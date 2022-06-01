#pragma once

#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_id.h>

//#define OSTREAM_EXPORTER_DEBUG
#define ENABLE_RLOG
// #define JAEGER_EXPORTER
#define ZIPKIN_EXPORTER

namespace tracing {

namespace jaeger {

// See more in jaeger-client-cpp
constexpr const char *kBinaryFormat = "trace-ctx";

} // namespace jaeger

// for built-in usage
constexpr const char *kTraceTagCmd = "cmd"; // attribute 中的保留字段
constexpr const char *kTraceTagUid = "uid"; // attribute 中的保留字段
constexpr const char *kTraceTagRot = "rot"; // attribute 中的保留字段
constexpr const char *kTraceTagErr = "err"; // attribute 中的保留字段

// ration [0, 10000]
constexpr unsigned kMaxRatioValue = 10000; // 采样率精确度 万分之一
// command [0, 65536 * 2)
constexpr unsigned kMaxCmdValue = 65536 * 2; // cmd 序号最大值 2^16-1
constexpr unsigned kMaxInterval = 60 * 5;    // 5 min 内必须采样一次

// config file path
constexpr const char *k_DefaultPathEnv = "TRACING_CTRL_CONF";  // 配置文件环境变量
constexpr const char *k_DefaultPath = "/etc/conf/tracing.yml"; // 配置文件默认地址

} // namespace tracing

namespace tracing {

inline std::string FormatTraceId(const opentelemetry::trace::TraceId &trace) noexcept {
    char buffer[2 * opentelemetry::trace::TraceId::kSize];
    trace.ToLowerBase16(buffer);
    return {buffer, sizeof(buffer)};
}

inline std::string FormatSpanId(const opentelemetry::trace::SpanId &span) noexcept {
    char buffer[2 * opentelemetry::trace::SpanId::kSize];
    span.ToLowerBase16(buffer);
    return {buffer, sizeof(buffer)};
}

} // namespace tracing