#pragma once

namespace tracing {

// See more in jaeger-client-cpp
constexpr const char *kTraceContextOfJaegerBinaryFormat = "trace-ctx"; // carrier 中的保留字段

// for built-in usage
constexpr const char *kTraceTagCmd = "cmd"; // attribute 中的保留字段
constexpr const char *kTraceTagUid = "uid"; // attribute 中的保留字段
constexpr const char *kTraceTagRot = "rot"; // attribute 中的保留字段
constexpr const char *kTraceTagErr = "err"; // attribute 中的保留字段

// ration [0, 10000]
constexpr unsigned kMaxRatioValue = 10000; // 采样率精确度 万分之一
// command [0, 65536)
constexpr unsigned kMaxCmdValue = 65536; // cmd 序号最大值 2^16
constexpr unsigned kMaxInterval = 60 * 5; // 5 min 内必须采样一次

// config file path
constexpr const char *kTraceConfEnv = "TRACING_CTRL_CONF";               // 配置文件环境变量
constexpr const char *kTraceConfPath = "/etc/conf/tracing-control.conf"; // 配置文件默认地址

} // namespace tracing
