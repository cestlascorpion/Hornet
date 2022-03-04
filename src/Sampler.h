#pragma once

#include <opentelemetry/sdk/trace/sampler.h>

namespace tracing {

using SampleResult = opentelemetry::sdk::trace::SamplingResult;

// CustomSampler 自定义根结点采样规则
class CustomSampler final : public opentelemetry::sdk::trace::Sampler {
public:
    CustomSampler() noexcept;

public:
    SampleResult ShouldSample(const opentelemetry::trace::SpanContext &context,              // parent span context
                              opentelemetry::trace::TraceId trace,                           // trace id
                              opentelemetry::nostd::string_view name,                        // operation name
                              opentelemetry::trace::SpanKind kind,                           // span kind
                              const opentelemetry::common::KeyValueIterable &attr,           // from StartSpan()
                              const opentelemetry::trace::SpanContextKeyValueIterable &link) // from StartSpan()
        noexcept override;
    opentelemetry::nostd::string_view GetDescription() const noexcept override;

private:
    const std::string _desc;
};

} // namespace tracing