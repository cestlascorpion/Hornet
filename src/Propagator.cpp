#include "Propagator.h"

#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/trace/context.h>

#include <cstring>

#include "Binary.h"
#include "Common.h"
#include "Tracing.h"

using namespace std;
using namespace opentelemetry;

namespace detail {

void Inject(const trace::SpanContext &ctx, context::propagation::TextMapCarrier &car) {
    Tracing::binary::Context data;
    memcpy(data.traceId.data(), ctx.trace_id().Id().data(), data.traceId.size());
    memcpy(data.spanId.data(), ctx.span_id().Id().data(), data.spanId.size());
    data.sampled = ctx.trace_flags().IsSampled();
    ctx.trace_state()->GetAllEntries([&](nostd::string_view key, nostd::string_view val) noexcept -> bool {
        data.baggage.emplace_back(string(key.data(), key.size()), string(val.data(), val.size()));
        return true;
    });

    string output;
    if (Tracing::binary::Format(data, output)) {
        car.Set(Tracing::jaeger::kBinaryFormat, output);
    }
}

trace::SpanContext Extract(const context::propagation::TextMapCarrier &car) {
    auto input = car.Get(Tracing::jaeger::kBinaryFormat);
    Tracing::binary::Context data;
    if (!Tracing::binary::Parse(string(input.data(), input.size()), data)) {
        return trace::SpanContext::GetInvalid();
    }

    trace::TraceId traceId({data.traceId.data(), data.traceId.size()});
    trace::SpanId spanId({data.spanId.data(), data.spanId.size()});
    trace::TraceFlags flag(data.sampled ? trace::TraceFlags::kIsSampled : 0);
    auto state = trace::TraceState::GetDefault();
    for (const auto &item : data.baggage) {
        state = state->Set(item.first, item.second);
    }
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
    binary::Context data;
    if (!binary::Parse(context, data)) {
        return;
    }

    trace::TraceId traceId({data.traceId.data(), data.traceId.size()});
    trace::SpanId spanId({data.spanId.data(), data.spanId.size()});
    trace::SpanId parentSpanId({data.parentSpanId.data(), data.parentSpanId.size()});

    if (traceId.IsValid()) {
        _traceId = FormatTraceId(traceId);
    }
    if (spanId.IsValid()) {
        _spanId = FormatSpanId(spanId);
    }
    if (parentSpanId.IsValid()) {
        _parentSpanId = FormatSpanId(parentSpanId);
    }
    _sampled = data.sampled;
    for (const auto &item : data.baggage) {
        _baggage.emplace(item.first, item.second);
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
    char traceText[trace::TraceId::kSize * 2];
    trace::TraceId(context.trace_id()).ToLowerBase16(traceText);
    char spanText[trace::SpanId::kSize * 2];
    trace::SpanId(context.span_id()).ToLowerBase16(spanText);

    _traceId = string(traceText, sizeof(traceText));
    _spanId = string(spanText, sizeof(spanText));
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
    binary::Context data;
    if (!binary::DecodeHex(context._traceId, data.traceId.data(), data.traceId.size()) ||
        !binary::DecodeHex(context._spanId, data.spanId.data(), data.spanId.size()) ||
        !binary::DecodeHex(context._parentSpanId, data.parentSpanId.data(), data.parentSpanId.size())) {
        return {};
    }
    data.sampled = context._sampled;
    for (const auto &item : context._baggage) {
        data.baggage.emplace_back(item.first, item.second);
    }
    string output;
    return binary::Format(data, output) ? output : string{};
}

} // namespace Tracing
