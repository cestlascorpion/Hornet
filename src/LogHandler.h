#pragma once

#include <opentelemetry/sdk/common/global_log_handler.h>

namespace tracing {

class CustomLogHandler : public opentelemetry::sdk::common::internal_log::LogHandler {
public:
    void Handle(opentelemetry::sdk::common::internal_log::LogLevel level, const char *file, int line, const char *msg,
                const opentelemetry::sdk::common::AttributeMap &attributes) noexcept override;
};

} // namespace tracing