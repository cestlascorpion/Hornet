#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Tracing {
namespace binary {

constexpr std::size_t kTraceIdSize = 16;
constexpr std::size_t kSpanIdSize = 8;
constexpr std::size_t kHeaderSize = kTraceIdSize + kSpanIdSize * 2u + 1u + sizeof(uint32_t);

struct Context {
    Context()
        : traceId()
        , spanId()
        , parentSpanId()
        , sampled(false)
        , baggage() {}

    std::array<uint8_t, kTraceIdSize> traceId;
    std::array<uint8_t, kSpanIdSize> spanId;
    std::array<uint8_t, kSpanIdSize> parentSpanId;
    bool sampled;
    std::vector<std::pair<std::string, std::string>> baggage;
};

bool DecodeHex(const std::string &hex, uint8_t *output, std::size_t size);
bool Parse(const std::string &input, Context &output);
bool Format(const Context &input, std::string &output);

} // namespace binary
} // namespace Tracing
