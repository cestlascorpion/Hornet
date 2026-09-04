#include "Binary.h"

#include <cstring>
#include <limits>

using namespace std;

namespace Tracing {
namespace binary {
namespace {

uint32_t ReadU32(const char *data) {
    return (uint32_t(uint8_t(data[0])) << 24u) | (uint32_t(uint8_t(data[1])) << 16u) |
           (uint32_t(uint8_t(data[2])) << 8u) | uint32_t(uint8_t(data[3]));
}

void WriteU32(char *data, uint32_t value) {
    data[0] = char((value >> 24u) & 0xffu);
    data[1] = char((value >> 16u) & 0xffu);
    data[2] = char((value >> 8u) & 0xffu);
    data[3] = char(value & 0xffu);
}

void AppendU32(string &data, uint32_t value) {
    auto offset = data.size();
    data.resize(offset + sizeof(value));
    WriteU32(&data[offset], value);
}

int HexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool HasBytes(size_t offset, size_t size, size_t total) {
    return offset <= total && size <= total - offset;
}

} // namespace

bool DecodeHex(const string &hex, uint8_t *output, size_t size) {
    if (hex.size() != size * 2u) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        auto high = HexValue(hex[i * 2u]);
        auto low = HexValue(hex[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[i] = static_cast<uint8_t>((high << 4u) | low);
    }
    return true;
}

bool Parse(const string &input, Context &output) {
    if (input.size() < kHeaderSize) {
        return false;
    }

    Context result;
    memcpy(result.traceId.data(), input.data(), result.traceId.size());
    memcpy(result.spanId.data(), input.data() + kTraceIdSize, result.spanId.size());
    memcpy(result.parentSpanId.data(), input.data() + kTraceIdSize + kSpanIdSize, result.parentSpanId.size());
    result.sampled = (uint8_t(input[kTraceIdSize + kSpanIdSize * 2u]) & 1u) != 0u;

    auto count = ReadU32(input.data() + kTraceIdSize + kSpanIdSize * 2u + 1u);
    size_t offset = kHeaderSize;
    for (uint32_t i = 0; i < count; ++i) {
        if (!HasBytes(offset, sizeof(uint32_t), input.size())) {
            return false;
        }
        auto keySize = ReadU32(input.data() + offset);
        offset += sizeof(uint32_t);
        if (!HasBytes(offset, keySize, input.size())) {
            return false;
        }
        auto key = string(input.data() + offset, keySize);
        offset += keySize;

        if (!HasBytes(offset, sizeof(uint32_t), input.size())) {
            return false;
        }
        auto valueSize = ReadU32(input.data() + offset);
        offset += sizeof(uint32_t);
        if (!HasBytes(offset, valueSize, input.size())) {
            return false;
        }
        auto value = string(input.data() + offset, valueSize);
        offset += valueSize;
        result.baggage.emplace_back(std::move(key), std::move(value));
    }

    if (offset != input.size()) {
        return false;
    }
    output = std::move(result);
    return true;
}

bool Format(const Context &input, string &output) {
    if (input.baggage.size() > numeric_limits<uint32_t>::max()) {
        return false;
    }

    string result(kHeaderSize, '\0');
    memcpy(&result[0], input.traceId.data(), input.traceId.size());
    memcpy(&result[kTraceIdSize], input.spanId.data(), input.spanId.size());
    memcpy(&result[kTraceIdSize + kSpanIdSize], input.parentSpanId.data(), input.parentSpanId.size());
    result[kTraceIdSize + kSpanIdSize * 2u] = input.sampled ? 1 : 0;
    WriteU32(&result[kTraceIdSize + kSpanIdSize * 2u + 1u], static_cast<uint32_t>(input.baggage.size()));

    for (const auto &item : input.baggage) {
        if (item.first.size() > numeric_limits<uint32_t>::max() ||
            item.second.size() > numeric_limits<uint32_t>::max()) {
            return false;
        }
        AppendU32(result, static_cast<uint32_t>(item.first.size()));
        result.append(item.first);
        AppendU32(result, static_cast<uint32_t>(item.second.size()));
        result.append(item.second);
    }

    output = std::move(result);
    return true;
}

} // namespace binary
} // namespace Tracing
