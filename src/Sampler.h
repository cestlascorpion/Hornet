#pragma once

#include <opentelemetry/sdk/trace/sampler.h>

namespace tracing {

// TLDR: just a alias
using SampleResult = opentelemetry::sdk::trace::SamplingResult;

class CustomSampler final : public opentelemetry::sdk::trace::Sampler {
public:
    CustomSampler() noexcept;

public:
    // ShouldSample: Decide whether the span should be collected
    SampleResult ShouldSample(const opentelemetry::trace::SpanContext &context,              // parent span context
                              opentelemetry::trace::TraceId trace,                           // trace id
                              opentelemetry::nostd::string_view name,                        // operation name
                              opentelemetry::trace::SpanKind kind,                           // span kind
                              const opentelemetry::common::KeyValueIterable &attr,           // from StartSpan()
                              const opentelemetry::trace::SpanContextKeyValueIterable &link) // from StartSpan()
        noexcept override;
    // GetDescription: Return the description of the sampler
    opentelemetry::nostd::string_view GetDescription() const noexcept override;

private:
    const std::string _desc;
};

} // namespace tracing