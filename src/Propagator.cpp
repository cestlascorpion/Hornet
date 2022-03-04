#include "Propagator.h"
#include "Common.h"
#include "Tracing.h"

#include <netinet/in.h>
#include <opentelemetry/trace/context.h>

using namespace std;
using namespace opentelemetry;

constexpr size_t kTraceLen = trace::TraceId::kSize;                            // 16 byte
constexpr size_t kSpanLen = trace::SpanId::kSize;                              // 8
constexpr size_t kFlagLen = sizeof(char);                                      // 1
constexpr size_t kSizeLen = sizeof(uint32_t);                                  // 4
constexpr size_t kBinCtxLen = kTraceLen + kSpanLen * 2u + kFlagLen + kSizeLen; // 37

namespace endian {

uint64_t htonll(uint64_t val) {
    return (((uint64_t)::htonl(val)) << 32u) + ::htonl(val >> 32u);
}

uint64_t ntohll(uint64_t val) {
    return (((uint64_t)::ntohl(val)) << 32u) + ::ntohl(val >> 32u);
}

} // namespace endian

namespace detail {

// context -> carrier
void Inject(const trace::SpanContext &ctx, context::propagation::TextMapCarrier &car) {
    // prepare buffer for trace-id span-id parent-span-id sample-flag baggage-number <- attention
    unsigned char buffer[kBinCtxLen];
    memset(buffer, 0, kBinCtxLen);
    // trace id
    auto high = endian::htonll(*(uint64_t *)ctx.trace_id().Id().data());
    auto low = endian::htonll(*(uint64_t *)(ctx.trace_id().Id().data() + kTraceLen / 2u));
    *(uint64_t *)buffer = high;
    *(uint64_t *)(buffer + kTraceLen / 2u) = low;
    // span id
    auto span = endian::htonll(*(uint64_t *)ctx.span_id().Id().data());
    *(uint64_t *)(buffer + kTraceLen) = span;
    // TODO: parent span id
    *(uint64_t *)(buffer + kTraceLen + kSpanLen) = 0;
    // flag
    buffer[kTraceLen + kSpanLen * 2u] = ctx.trace_flags().IsSampled() ? '1' : '0';
    // fast return
    if (ctx.trace_state()->Empty()) {
        car.Set(tracing::kTraceContextOfJaegerBinaryFormat, nostd::string_view((char *)buffer, kBinCtxLen));
        return;
    }
    // get all baggage into content
    stringstream content;
    uint32_t num = 0u;
    unsigned char size[kSizeLen];
    ctx.trace_state()->GetAllEntries([&](nostd::string_view key, nostd::string_view val) noexcept -> bool {
        // memset(size, 0, kSizeLen);
        *((uint32_t *)size) = (uint32_t)key.size();
        content << string((char *)size, kSizeLen) << string(key.data(), key.size());
        // memset(size, 0, kSizeLen);
        *((uint32_t *)size) = (uint32_t)val.size();
        content << string((char *)size, kSizeLen) << string(val.data(), val.size());
        ++num;
        return true;
    });
    // do not forget to correct baggage number
    *((uint32_t *)(&buffer[kTraceLen + kSpanLen * 2u + kFlagLen])) = num;
    // construct trace context all-in-one
    stringstream context;
    context << string((char *)buffer, kBinCtxLen) << content.str();
    car.Set(tracing::kTraceContextOfJaegerBinaryFormat, context.str());
}

// carrier -> context
trace::SpanContext Extract(const context::propagation::TextMapCarrier &car) {
    // get trace context all-in-one
    auto context = car.Get(tracing::kTraceContextOfJaegerBinaryFormat);
    // fast return
    if (context.empty() || context.size() < kBinCtxLen) {
        return trace::SpanContext::GetInvalid();
    }
    // trace id
    auto high = endian::ntohll(*(uint64_t *)context.data());
    auto low = endian::ntohll(*(uint64_t *)(context.data() + kTraceLen / 2u));
    *(uint64_t *)context.data() = high;
    *(uint64_t *)(context.data() + kTraceLen / 2u) = low;
    trace::TraceId traceId({(uint8_t *)context.data(), kTraceLen});
    // span id
    auto span = endian::ntohll(*(uint64_t *)(context.data() + kTraceLen));
    *(uint64_t *)(context.data() + kTraceLen) = span;
    trace::SpanId spanId({(uint8_t *)(context.data() + kTraceLen), kSpanLen});
    // parend span id
    auto parent = endian::ntohll(*(uint64_t *)(context.data() + kTraceLen + kSpanLen));
    *(uint64_t *)(context.data() + kTraceLen + kSpanLen) = parent;
    trace::SpanId parentId({(uint8_t *)(context.data() + kTraceLen + kSpanLen), kSpanLen});
    // flag
    trace::TraceFlags flag{*((uint8_t *)(context.data() + kTraceLen + kSpanLen * 2u))};
    // get number of baggage which is well-known as trace-state
    unsigned char size[kSizeLen];
    memcpy(size, context.data() + kTraceLen + kSpanLen * 2u + kFlagLen, kSizeLen);
    auto baggage = *((uint32_t *)size);
    // fast return
    if (baggage == 0u) {
        return {traceId, spanId, flag, true};
    }
    // get all baggage
    auto state = trace::TraceState::GetDefault();
    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        // get the key
        if (offset + kSizeLen > context.size()) {
            return trace::SpanContext::GetInvalid();
        }
        auto keySize = *(uint32_t *)nostd::string_view(context.data() + offset, kSizeLen).data();
        offset += kSizeLen;
        if (offset + keySize > context.size()) {
            return trace::SpanContext::GetInvalid();
        }
        auto key = string(context.data() + offset, keySize);
        offset += keySize;
        // get the value
        if (offset + kSizeLen > context.size()) {
            return trace::SpanContext::GetInvalid();
        }
        auto valSize = *(uint32_t *)(nostd::string_view(context.data() + offset, kSizeLen).data());
        offset += kSizeLen;
        if (offset + valSize > context.size()) {
            return trace::SpanContext::GetInvalid();
        }
        auto val = string(context.data() + offset, valSize);
        offset += valSize;
        // write into trace state
        state = state->Set(key, val);
    }
    // finally
    return {traceId, spanId, flag, true, move(state)};
}

} // namespace detail

namespace tracing {

nostd::string_view CustomCarrier::Get(nostd::string_view key) const noexcept {
    auto it = _headers.find(key);
    if (it != _headers.end()) {
        return it->second;
    }
    return "";
}

void CustomCarrier::Set(nostd::string_view key, nostd::string_view value) noexcept {
    _headers[key] = {value.data(), value.size()};
}

void CustomPropagator::Inject(context::propagation::TextMapCarrier &carrier, const context::Context &context) noexcept {
    auto spanContext = trace::GetSpan(context)->GetContext();
    if (!spanContext.IsValid()) {
        return;
    }
    detail::Inject(spanContext, carrier);
}

context::Context CustomPropagator::Extract(const context::propagation::TextMapCarrier &carrier,
                                           context::Context &context) noexcept {
    auto spanContext = detail::Extract(carrier);
    nostd::shared_ptr<trace::Span> sp(new trace::DefaultSpan(spanContext));
    return trace::SetSpan(context, sp);
}

bool CustomPropagator::Fields(nostd::function_ref<bool(nostd::string_view)> callback) const noexcept {
    return callback(kTraceContextOfJaegerBinaryFormat);
}

} // namespace tracing

namespace tracing {

Context::Context(const string &context)
    : _traceId("0")
    , _spanId("0")
    , _parentSpanId("0")
    , _sampled(false)
    , _baggage() {
    if (context.empty() || context.size() < kBinCtxLen) {
        return;
    }
    trace::TraceId traceId({(uint8_t *)context.data(), kTraceLen});
    auto high = endian::htonll(*(uint64_t *)traceId.Id().data());
    auto low = endian::htonll(*(uint64_t *)(traceId.Id().data() + kTraceLen / 2u));
    *(uint64_t *)traceId.Id().data() = high;
    *(uint64_t *)(traceId.Id().data() + kTraceLen / 2u) = low;
    trace::SpanId spanId({(uint8_t *)(context.data() + kTraceLen), kSpanLen});
    auto span = endian::htonll(*(uint64_t *)spanId.Id().data());
    *(uint64_t *)spanId.Id().data() = span;
    trace::SpanId parendId({(uint8_t *)(context.data() + kTraceLen + kSpanLen), kSpanLen});
    auto parent = endian::htonll(*(uint64_t *)parendId.Id().data());
    *(uint64_t *)parendId.Id().data() = parent;
    trace::TraceFlags flag{*((uint8_t *)(context.data() + kTraceLen + kSpanLen * 2u))};

    constexpr const size_t length = kTraceLen * 2u + kSpanLen * 2u + kSpanLen * 2u;
    char buffer[length];
    memset(buffer, 0, length);
    traceId.ToLowerBase16(nostd::span<char, kTraceLen * 2u>{&buffer[0], kTraceLen * 2u});
    spanId.ToLowerBase16(nostd::span<char, kSpanLen * 2u>{&buffer[kTraceLen * 2u], kSpanLen * 2u});
    parendId.ToLowerBase16(nostd::span<char, kSpanLen * 2u>{&buffer[kTraceLen * 2u + kSpanLen * 2u], kSpanLen * 2u});

    if (traceId.IsValid()) {
        _traceId = string(&buffer[0], kTraceLen * 2u);
    }
    if (spanId.IsValid()) {
        _spanId = string(&buffer[kTraceLen * 2u], kSpanLen * 2u);
    }
    if (parendId.IsValid()) {
        _parentSpanId = string(&buffer[kTraceLen * 2u + kSpanLen * 2u], kSpanLen * 2u);
    }
    _sampled = flag.IsSampled();

    unsigned char size[kSizeLen];
    memcpy(size, context.data() + kTraceLen + kSpanLen * 2u + kFlagLen, kSizeLen);
    auto baggage = *((uint32_t *)size);
    if (baggage == 0) {
        return;
    }
    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        if (offset + kSizeLen > context.size()) {
            return;
        }
        auto keySize = *(uint32_t *)nostd::string_view(context.data() + offset, kSizeLen).data();
        offset += kSizeLen;
        if (offset + keySize > context.size()) {
            return;
        }
        auto key = string(context.data() + offset, keySize);
        offset += keySize;
        if (offset + kSizeLen > context.size()) {
            return;
        }
        auto valSize = *(uint32_t *)(nostd::string_view(context.data() + offset, kSizeLen).data());
        offset += kSizeLen;
        if (offset + valSize > context.size()) {
            return;
        }
        auto val = string(context.data() + offset, valSize);
        offset += valSize;
        _baggage.emplace(move(key), move(val));
    }
}

} // namespace tracing