#include "LogHandler.h"

#include "Common.h"

using namespace opentelemetry;

namespace tracing {

void CustomLogHandler::Handle(sdk::common::internal_log::LogLevel level, const char *file, int line, const char *msg,
                              const sdk::common::AttributeMap &attributes) noexcept {
    std::stringstream output_s;
    output_s << "[" << sdk::common::internal_log::LevelToString(level) << "] ";
    if (file != nullptr) {
        output_s << "File: " << file << ":" << line;
    }
    if (msg != nullptr) {
        output_s << msg;
    }
    output_s << std::endl;
    // TBD - print attributes
    std::cout << output_s.str(); // thread safe.
}

} // namespace tracing