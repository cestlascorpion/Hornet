#include "Propagator.h"

#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/trace/context.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "Common.h"
#include "Tracing.h"

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

namespace detail {

uint32_t ReadU32(const char *data) {
    return (uint32_t(uint8_t(data[0])) << 24u) | (uint32_t(uint8_t(data[1])) << 16u) |
           (uint32_t(uint8_t(data[2])) << 8u) | uint32_t(uint8_t(data[3]));
}

void AppendU32(string &data, uint32_t value) {
    data.push_back(char((value >> 24u) & 0xffu));
    data.push_back(char((value >> 16u) & 0xffu));
    data.push_back(char((value >> 8u) & 0xffu));
    data.push_back(char(value & 0xffu));
}

int HexToInt(char c) {
    return kHexDigits[uint8_t(c)];
}

bool HexToBinary(const string &hex, uint8_t *buffer, size_t buffer_size) {
    memset(buffer, 0, buffer_size);
    if (hex.empty()) {
        return true;
    }
    if (hex.size() > buffer_size * 2) {
        return false;
    }
    auto hex_size = (hex.size());
    auto buffer_pos = buffer_size - (hex_size + 1) / 2;
    auto last_hex_pos = hex_size - 1;
    auto i = 0u;
    for (; i < last_hex_pos; i += 2) {
        auto high = HexToInt(hex[i]);
        auto low = HexToInt(hex[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        buffer[buffer_pos++] = static_cast<uint8_t>((high << 4) | low);
    }
    if (i == last_hex_pos) {
        auto value = HexToInt(hex[i]);
        if (value < 0) {
            return false;
        }
        buffer[buffer_pos] = static_cast<uint8_t>(value);
    }
    return true;
}

// Inject: context -> carrier
void Inject(const trace::SpanContext &ctx, context::propagation::TextMapCarrier &car) {
    // prepare buffer for trace-id span-id parent-span-id sample-flag baggage-number <- attention
    array<uint8_t, kBinCtxLen> buffer{};

    // trace id
    memcpy(buffer.data(), ctx.trace_id().Id().data(), kTraceLen);

    // span id
    memcpy(buffer.data() + kTraceLen, ctx.span_id().Id().data(), kSpanLen);

    // flag
    buffer[kTraceLen + kSpanLen * 2u] = ctx.trace_flags().IsSampled() ? 1u : 0u;

    // fast return
    if (ctx.trace_state()->Empty()) {
        car.Set(Tracing::jaeger::kBinaryFormat,
                nostd::string_view(reinterpret_cast<const char *>(buffer.data()), buffer.size()));
        return;
    }

    // get all baggage into content, NOT SPECIFIED BY THE SPEC!
    stringstream content;
    uint32_t num = 0u;
    ctx.trace_state()->GetAllEntries([&](nostd::string_view key, nostd::string_view val) noexcept -> bool {
        auto keySize = static_cast<uint32_t>(key.size());
        auto valSize = static_cast<uint32_t>(val.size());
        string data;
        AppendU32(data, keySize);
        content << data << string(key.data(), key.size());
        data.clear();
        AppendU32(data, valSize);
        content << data << string(val.data(), val.size());
        ++num;
        return true;
    });

    // DO NOT forget to correct baggage number
    auto header = string(reinterpret_cast<const char *>(buffer.data()), buffer.size());
    header.resize(kTraceLen + kSpanLen * 2u + kFlagLen);
    AppendU32(header, num);

    // construct trace context all-in-one
    stringstream context;
    context << header << content.str();
    car.Set(Tracing::jaeger::kBinaryFormat, context.str());
}

// Extract: carrier -> context
trace::SpanContext Extract(const context::propagation::TextMapCarrier &car) {
    // get jaeger trace context all-in-one
    auto context = car.Get(Tracing::jaeger::kBinaryFormat);

    // fast return
    if (context.empty() || context.size() < kBinCtxLen) {
        return trace::SpanContext::GetInvalid();
    }

    array<uint8_t, kTraceLen> trace{};
    memcpy(trace.data(), context.data(), trace.size());
    trace::TraceId traceId({trace.data(), trace.size()});

    array<uint8_t, kSpanLen> span{};
    memcpy(span.data(), context.data() + kTraceLen, span.size());
    trace::SpanId spanId({span.data(), span.size()});

    // flag
    trace::TraceFlags flag{uint8_t(context[kTraceLen + kSpanLen * 2u])};

    // get number of baggage which is well-known as trace-state
    auto baggage = ReadU32(context.data() + kTraceLen + kSpanLen * 2u + kFlagLen);

    // fast return
    if (baggage == 0u) {
        return {traceId, spanId, flag, true};
    }

    // get all baggage, NOT SPECIFIED BY THE SPEC!
    auto state = trace::TraceState::GetDefault();
    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        // get the key
        if (offset > context.size() || kSizeLen > context.size() - offset) {
            return trace::SpanContext::GetInvalid();
        }
        auto keySize = ReadU32(context.data() + offset);
        offset += kSizeLen;
        if (offset > context.size() || keySize > context.size() - offset) {
            return trace::SpanContext::GetInvalid();
        }
        auto key = string(context.data() + offset, keySize);
        offset += keySize;
        // get the value
        if (offset > context.size() || kSizeLen > context.size() - offset) {
            return trace::SpanContext::GetInvalid();
        }
        auto valSize = ReadU32(context.data() + offset);
        offset += kSizeLen;
        if (offset > context.size() || valSize > context.size() - offset) {
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

namespace Tracing {

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

} // namespace Tracing

namespace Tracing {

Context::Context(const string &context)
    : _traceId("00000000000000000000000000000000")
    , _spanId("0000000000000000")
    , _parentSpanId("0000000000000000")
    , _sampled(false)
    , _baggage() {
    if (context.empty() || context.size() < kBinCtxLen) {
        return;
    }

    array<uint8_t, kTraceLen> trace{};
    memcpy(trace.data(), context.data(), trace.size());
    trace::TraceId traceId({trace.data(), trace.size()});

    array<uint8_t, kSpanLen> span{};
    memcpy(span.data(), context.data() + kTraceLen, span.size());
    trace::SpanId spanId({span.data(), span.size()});

    trace::TraceFlags flag{uint8_t(context[kTraceLen + kSpanLen * 2u])};

    constexpr const size_t length = kTraceLen * 2u + kSpanLen * 2u + kSpanLen * 2u;
    char buffer[length];
    memset(buffer, 0, length);

    traceId.ToLowerBase16(nostd::span<char, kTraceLen * 2u>{&buffer[0], kTraceLen * 2u});
    spanId.ToLowerBase16(nostd::span<char, kSpanLen * 2u>{&buffer[kTraceLen * 2u], kSpanLen * 2u});

    if (traceId.IsValid()) {
        _traceId = string(&buffer[0], kTraceLen * 2u);
    }
    if (spanId.IsValid()) {
        _spanId = string(&buffer[kTraceLen * 2u], kSpanLen * 2u);
    }
    _sampled = flag.IsSampled();

    auto baggage = detail::ReadU32(context.data() + kTraceLen + kSpanLen * 2u + kFlagLen);
    if (baggage == 0) {
        return;
    }

    size_t offset = kBinCtxLen;
    for (auto i = 0u; i < baggage; i++) {
        if (offset > context.size() || kSizeLen > context.size() - offset) {
            return;
        }
        auto keySize = detail::ReadU32(context.data() + offset);
        offset += kSizeLen;
        if (offset > context.size() || keySize > context.size() - offset) {
            return;
        }
        auto key = string(context.data() + offset, keySize);
        offset += keySize;
        if (offset > context.size() || kSizeLen > context.size() - offset) {
            return;
        }
        auto valSize = detail::ReadU32(context.data() + offset);
        offset += kSizeLen;
        if (offset > context.size() || valSize > context.size() - offset) {
            return;
        }
        auto val = string(context.data() + offset, valSize);
        offset += valSize;
        _baggage.emplace(move(key), move(val));
    }
}

Context::Context(const trace::SpanContext &context)
    : _traceId("00000000000000000000000000000000")
    , _spanId("0000000000000000")
    , _parentSpanId("0000000000000000")
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
    if (context._traceId.size() != kTraceLen * 2u || context._spanId.size() != kSpanLen * 2u ||
        context._parentSpanId.size() != kSpanLen * 2u) {
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

} // namespace Tracing
