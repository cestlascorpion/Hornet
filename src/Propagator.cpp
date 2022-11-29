#include "Propagator.h"
#include "Common.h"
#include "Tracing.h"

#include <endian.h>

#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/trace/context.h>

using namespace std;
using namespace opentelemetry;

constexpr size_t kTraceLen = trace::TraceId::kSize;                            // 16 byte
constexpr size_t kSpanLen = trace::SpanId::kSize;                              // 8
constexpr size_t kFlagLen = sizeof(char);                                      // 1
constexpr size_t kSizeLen = sizeof(uint32_t);                                  // 4
constexpr size_t kBinCtxLen = kTraceLen + kSpanLen * 2u + kFlagLen + kSizeLen; // 37

constexpr int8_t kHexDigits[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

namespace endian {

uint16_t toBigEndian(uint16_t value) {
    return htobe16(value);
}

uint32_t toBigEndian(uint32_t value) {
    return htobe32(value);
}

uint64_t toBigEndian(uint64_t value) {
    return htobe64(value);
}

uint16_t fromBigEndian(uint16_t value) {
    return be16toh(value);
}

uint32_t fromBigEndian(uint32_t value) {
    return be32toh(value);
}

uint64_t fromBigEndian(uint64_t value) {
    return be64toh(value);
}

} // namespace endian

namespace detail {

unsigned char HexToInt(char c) {
    return (unsigned char)kHexDigits[uint8_t(c)];
}

bool HexToBinary(const string &hex, uint8_t *buffer, size_t buffer_size) {
    memset(buffer, 0, buffer_size);
    if (hex.size() > buffer_size * 2) {
        return false;
    }
    auto hex_size = (hex.size());
    auto buffer_pos = buffer_size - (hex_size + 1) / 2;
    auto last_hex_pos = hex_size - 1;
    auto i = 0u;
    for (; i < last_hex_pos; i += 2) {
        buffer[buffer_pos++] = static_cast<uint8_t>((HexToInt(hex[i]) << 4) | HexToInt(hex[i + 1]));
    }
    if (i == last_hex_pos) {
        buffer[buffer_pos] = HexToInt(hex[i]);
    }
    return true;
}

// Inject: context -> carrier
void Inject(const trace::SpanContext &ctx, context::propagation::TextMapCarrier &car) {
    // prepare buffer for trace-id span-id parent-span-id sample-flag baggage-number <- attention
    unsigned char buffer[kBinCtxLen];
    memset(buffer, 0, kBinCtxLen);

    // trace id
    auto high = endian::toBigEndian(*(uint64_t *)ctx.trace_id().Id().data());
    auto low = endian::toBigEndian(*(uint64_t *)(ctx.trace_id().Id().data() + kTraceLen / 2u));
    *(uint64_t *)buffer = high;
    *(uint64_t *)(buffer + kTraceLen / 2u) = low;

    // span id
    auto span = endian::toBigEndian(*(uint64_t *)ctx.span_id().Id().data());
    *(uint64_t *)(buffer + kTraceLen) = span;

    // parent span id: unnecessary
    // *(uint64_t *)(buffer + kTraceLen + kSpanLen) = 0;

    // flag
    buffer[kTraceLen + kSpanLen * 2u] = ctx.trace_flags().IsSampled() ? '1' : '0';

    // fast return
    if (ctx.trace_state()->Empty()) {
        car.Set(tracing::jaeger::kBinaryFormat, nostd::string_view((char *)buffer, kBinCtxLen));
        return;
    }

    // get all baggage into content, NOT SPECIFIED BY THE SPEC!
    stringstream content;
    uint32_t num = 0u;
    unsigned char size[kSizeLen];
    ctx.trace_state()->GetAllEntries([&](nostd::string_view key, nostd::string_view val) noexcept -> bool {
        // memset(size, 0, kSizeLen);
        *((uint32_t *)size) = endian::toBigEndian((uint32_t)key.size());
        content << string((char *)size, kSizeLen) << string(key.data(), key.size());
        // memset(size, 0, kSizeLen);
        *((uint32_t *)size) = endian::toBigEndian((uint32_t)val.size());
        content << string((char *)size, kSizeLen) << string(val.data(), val.size());
        ++num;
        return true;
    });

    // DO NOT forget to correct baggage number
    *((uint32_t *)(&buffer[kTraceLen + kSpanLen * 2u + kFlagLen])) = endian::toBigEndian(num);

    // construct trace context all-in-one
    stringstream context;
    context << string((char *)buffer, kBinCtxLen) << content.str();
    car.Set(tracing::jaeger::kBinaryFormat, context.str());
}

// Extract: carrier -> context
trace::SpanContext Extract(const context::propagation::TextMapCarrier &car) {
    // get jaeger trace context all-in-one
    auto context = car.Get(tracing::jaeger::kBinaryFormat);

    // fast return
    if (context.empty() || context.size() < kBinCtxLen) {
        return trace::SpanContext::GetInvalid();
    }

    // trace id
    auto high = endian::fromBigEndian(*(uint64_t *)context.data());
    auto low = endian::fromBigEndian(*(uint64_t *)(context.data() + kTraceLen / 2u));
    *(uint64_t *)context.data() = high;
    *(uint64_t *)(context.data() + kTraceLen / 2u) = low;
    trace::TraceId traceId({(uint8_t *)context.data(), kTraceLen});

    // span id
    auto span = endian::fromBigEndian(*(uint64_t *)(context.data() + kTraceLen));
    *(uint64_t *)(context.data() + kTraceLen) = span;
    trace::SpanId spanId({(uint8_t *)(context.data() + kTraceLen), kSpanLen});

    // parend span id: unnecessary
    // auto parent = endian::fromBigEndian(*(uint64_t *)(context.data() + kTraceLen + kSpanLen));
    // *(uint64_t *)(context.data() + kTraceLen + kSpanLen) = parent;
    // trace::SpanId parentId({(uint8_t *)(context.data() + kTraceLen + kSpanLen), kSpanLen});

    // flag
    trace::TraceFlags flag{*((uint8_t *)(context.data() + kTraceLen + kSpanLen * 2u))};

    // get number of baggage which is well-known as trace-state
    unsigned char size[kSizeLen];
    memcpy(size, context.data() + kTraceLen + kSpanLen * 2u + kFlagLen, kSizeLen);
    auto baggage = endian::fromBigEndian(*((uint32_t *)size));

    // fast return
    if (baggage == 0u) {
        return {traceId, spanId, flag, true};
    }

    // get all baggage, NOT SPECIFIED BY THE SPEC!
    auto state = trace::TraceState::GetDefault();
    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        // get the key
        if (offset + kSizeLen > context.size()) {
            return trace::SpanContext::GetInvalid();
        }
        auto keySize = endian::fromBigEndian(*(uint32_t *)nostd::string_view(context.data() + offset, kSizeLen).data());
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
        auto valSize =
            endian::fromBigEndian(*(uint32_t *)(nostd::string_view(context.data() + offset, kSizeLen).data()));
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
    return callback(jaeger::kBinaryFormat);
}

} // namespace tracing

namespace tracing {

Context::Context(const string &context)
    : _traceId("000000000000000000")
    , _spanId("000000000")
    , _parentSpanId("000000000")
    , _sampled(false)
    , _baggage() {
    if (context.empty() || context.size() < kBinCtxLen) {
        return;
    }

    trace::TraceId traceId({(uint8_t *)context.data(), kTraceLen});
    auto high = endian::fromBigEndian(*(uint64_t *)traceId.Id().data());
    auto low = endian::fromBigEndian(*(uint64_t *)(traceId.Id().data() + kTraceLen / 2u));
    *(uint64_t *)traceId.Id().data() = high;
    *(uint64_t *)(traceId.Id().data() + kTraceLen / 2u) = low;

    trace::SpanId spanId({(uint8_t *)(context.data() + kTraceLen), kSpanLen});
    auto span = endian::fromBigEndian(*(uint64_t *)spanId.Id().data());
    *(uint64_t *)spanId.Id().data() = span;

    // trace::SpanId parendId({(uint8_t *)(context.data() + kTraceLen + kSpanLen), kSpanLen});
    // auto parent = endian::fromBigEndian(*(uint64_t *)parendId.Id().data());
    // *(uint64_t *)parendId.Id().data() = parent;

    trace::TraceFlags flag{*((uint8_t *)(context.data() + kTraceLen + kSpanLen * 2u))};

    constexpr const size_t length = kTraceLen * 2u + kSpanLen * 2u + kSpanLen * 2u;
    char buffer[length];
    memset(buffer, 0, length);

    traceId.ToLowerBase16(nostd::span<char, kTraceLen * 2u>{&buffer[0], kTraceLen * 2u});
    spanId.ToLowerBase16(nostd::span<char, kSpanLen * 2u>{&buffer[kTraceLen * 2u], kSpanLen * 2u});
    // parendId.ToLowerBase16(nostd::span<char, kSpanLen * 2u>{&buffer[kTraceLen * 2u + kSpanLen * 2u], kSpanLen * 2u});

    if (traceId.IsValid()) {
        _traceId = string(&buffer[0], kTraceLen * 2u);
    }
    if (spanId.IsValid()) {
        _spanId = string(&buffer[kTraceLen * 2u], kSpanLen * 2u);
    }
    // if (parendId.IsValid()) {
    //     _parentSpanId = string(&buffer[kTraceLen * 2u + kSpanLen * 2u], kSpanLen * 2u);
    // }
    _sampled = flag.IsSampled();

    unsigned char size[kSizeLen];
    memcpy(size, context.data() + kTraceLen + kSpanLen * 2u + kFlagLen, kSizeLen);
    auto baggage = endian::fromBigEndian(*((uint32_t *)size));
    if (baggage == 0) {
        return;
    }

    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        if (offset + kSizeLen > context.size()) {
            return;
        }
        auto keySize = endian::fromBigEndian(*(uint32_t *)nostd::string_view(context.data() + offset, kSizeLen).data());
        offset += kSizeLen;
        if (offset + keySize > context.size()) {
            return;
        }
        auto key = string(context.data() + offset, keySize);
        offset += keySize;
        if (offset + kSizeLen > context.size()) {
            return;
        }
        auto valSize = endian::fromBigEndian(*(uint32_t *)(nostd::string_view(context.data() + offset, kSizeLen).data()));
        offset += kSizeLen;
        if (offset + valSize > context.size()) {
            return;
        }
        auto val = string(context.data() + offset, valSize);
        offset += valSize;
        _baggage.emplace(move(key), move(val));
    }
}

Context::Context(const trace::SpanContext &context)
    : _traceId("000000000000000000")
    , _spanId("000000000")
    , _parentSpanId("000000000")
    , _sampled(false)
    , _baggage() {
    if (!context.IsValid()) {
        return;
    }
    char trace[kTraceLen * 2];
    trace::TraceId(context.trace_id()).ToLowerBase16(trace);
    char span[kSpanLen * 2];
    trace::SpanId(context.span_id()).ToLowerBase16(span);

    _traceId = string(trace, kTraceLen * 2);
    _spanId = string(span, kSpanLen * 2);
    _sampled = context.IsSampled();

    context.trace_state()->GetAllEntries([&](nostd::string_view key, nostd::string_view val) noexcept -> bool {
        _baggage.emplace(string(key.data(), key.size()), string(val.data(), val.size()));
        return true;
    });
}

Context::Context(const string &traceId, const string &spanId, const string &parentSpanId, bool sampled,
                 const map<string, string> &baggage)
    : _traceId(traceId)
    , _spanId(spanId)
    , _parentSpanId(parentSpanId)
    , _sampled(sampled)
    , _baggage(baggage) {}

Context Tracing::GetPlainTextContext() noexcept {
    auto ctx = context::RuntimeContext::GetCurrent();
    return Context(trace::GetSpan(ctx)->GetContext());
}

string Tracing::GetJaegerContext() noexcept {
    auto pr = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto ctx = context::RuntimeContext::GetCurrent();
    CustomCarrier carrier;
    pr->Inject(carrier, ctx);

    auto tc = carrier.Get(jaeger::kBinaryFormat);
    return {tc.data(), tc.size()};
}

Context Tracing::ParseFromJaegerContext(const string &context) noexcept {
    return Context(context);
}

string Tracing::FormatAsJaegerContext(const Context &context) noexcept {
    if (context._traceId.size() != kTraceLen * 2u || context._spanId.size() != kSpanLen * 2u) {
        return {};
    }
    unsigned char buffer[kTraceLen + kSpanLen + kSpanLen];
    if (!detail::HexToBinary(context._traceId, buffer, kTraceLen)) {
        return {};
    }
    trace::TraceId traceId({(uint8_t *)buffer, kTraceLen});
    if (!detail::HexToBinary(context._spanId, buffer + kTraceLen, kSpanLen)) {
        return {};
    }
    trace::SpanId spanId({(uint8_t *)buffer + kTraceLen, kSpanLen});
    if (!detail::HexToBinary(context._parentSpanId, buffer + kTraceLen + kSpanLen, kSpanLen)) {
        return {};
    }
    trace::SpanId parent({(uint8_t *)buffer + kTraceLen + kSpanLen, kSpanLen});
    trace::TraceFlags flag(context._sampled ? trace::TraceFlags::kIsSampled : 0);
    auto state = trace::TraceState::GetDefault();
    for (const auto &item : context._baggage) {
        state = state->Set(item.first, item.second);
    }

    trace::SpanContext ctx(traceId, spanId, flag, true, state);
    CustomCarrier carrier;
    detail::Inject(ctx, carrier);

    auto tc = carrier.Get(jaeger::kBinaryFormat);
    return {tc.data(), tc.size()};
}

} // namespace tracing
