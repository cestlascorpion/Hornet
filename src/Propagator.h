#pragma once

#include <opentelemetry/context/propagation/text_map_propagator.h>

#include <map>

namespace tracing {

class CustomCarrier final : public opentelemetry::context::propagation::TextMapCarrier {
public:
    // Get: Return the value associated with the key if it exists.
    opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key) const noexcept override;
    // Set: Associate the value with the key, replacing any existing value.
    void Set(opentelemetry::nostd::string_view key, opentelemetry::nostd::string_view value) noexcept override;

private:
    std::map<opentelemetry::nostd::string_view, std::string> _headers;
};

class CustomPropagator final : public opentelemetry::context::propagation::TextMapPropagator {
public:
    // Inject: Inject the context into the carrier.
    void Inject(opentelemetry::context::propagation::TextMapCarrier &carrier,
                const opentelemetry::context::Context &context) noexcept override;
    // Extract: Extract the context from the carrier.
    opentelemetry::context::Context Extract(const opentelemetry::context::propagation::TextMapCarrier &carrier,
                                            opentelemetry::context::Context &context) noexcept override;
    // Fields: Call the callback for each key in the carrier. The callback should return true to continue iterating, or
    // false to stop.
    bool Fields(
        opentelemetry::nostd::function_ref<bool(opentelemetry::nostd::string_view)> callback) const noexcept override;
};

} // namespace tracing
