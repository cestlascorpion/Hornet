#pragma once

#include <opentelemetry/context/propagation/text_map_propagator.h>

#include <map>

namespace tracing {

class CustomCarrier final : public opentelemetry::context::propagation::TextMapCarrier {
public:
    opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key) const noexcept override;
    void Set(opentelemetry::nostd::string_view key, opentelemetry::nostd::string_view value) noexcept override;

private:
    std::map<opentelemetry::nostd::string_view, std::string> _headers;
};

class CustomPropagator final : public opentelemetry::context::propagation::TextMapPropagator {
public:
    void Inject(opentelemetry::context::propagation::TextMapCarrier &carrier, // return value
                const opentelemetry::context::Context &context)               // current context
        noexcept override;
    opentelemetry::context::Context
    Extract(const opentelemetry::context::propagation::TextMapCarrier &carrier, // remote context
            opentelemetry::context::Context &context)                           // return value
        noexcept override;

    bool Fields(
        opentelemetry::nostd::function_ref<bool(opentelemetry::nostd::string_view)> callback) const noexcept override;
};

} // namespace tracing
