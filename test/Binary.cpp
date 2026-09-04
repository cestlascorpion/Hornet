#include <cstdint>
#include <string>

#include "Binary.h"

using namespace std;
using namespace Tracing::binary;

constexpr const char *kContextHex =
    "FEA80376EE0C6F9FEC9C673F09F6EAE1"
    "9E5304AE5F1682BB"
    "0000000000000000"
    "01"
    "00000001"
    "00000008"
    "70796A5F74657374"
    "0000000B"
    "68656C6C6F20776F726C64";

#define CHECK(expr) \
    do {            \
        if (!(expr)) { \
            return __LINE__; \
        } \
    } while (false)

bool ToBinary(const string &hex, string &output) {
    output.assign(hex.size() / 2u, '\0');
    return DecodeHex(hex, reinterpret_cast<uint8_t *>(&output[0]), output.size());
}

int main() {
    string wire;
    CHECK(ToBinary(kContextHex, wire));
    Context context;
    CHECK(Parse(wire, context));
    CHECK(context.traceId[0] == 0xfeu);
    CHECK(context.traceId[15] == 0xe1u);
    CHECK(context.spanId[0] == 0x9eu);
    CHECK(context.spanId[7] == 0xbbu);
    CHECK(context.sampled);
    CHECK(context.baggage.size() == 1u);
    CHECK(context.baggage[0].first == "pyj_test");
    CHECK(context.baggage[0].second == "hello world");

    string encoded;
    CHECK(Format(context, encoded));
    CHECK(encoded == wire);

    Context custom;
    CHECK(DecodeHex("00112233445566778899AABBCCDDEEFF", custom.traceId.data(), custom.traceId.size()));
    CHECK(DecodeHex("1122334455667788", custom.spanId.data(), custom.spanId.size()));
    CHECK(DecodeHex("8877665544332211", custom.parentSpanId.data(), custom.parentSpanId.size()));
    custom.baggage.emplace_back("region", "cn");
    custom.baggage.emplace_back("mode", "test");

    CHECK(Format(custom, encoded));
    Context parsed;
    CHECK(Parse(encoded, parsed));
    CHECK(parsed.traceId == custom.traceId);
    CHECK(parsed.spanId == custom.spanId);
    CHECK(parsed.parentSpanId == custom.parentSpanId);
    CHECK(!parsed.sampled);
    CHECK(parsed.baggage == custom.baggage);

    uint8_t byte = 0;
    CHECK(DecodeHex("fF", &byte, 1u));
    CHECK(byte == 0xffu);
    CHECK(!DecodeHex("f", &byte, 1u));
    CHECK(!DecodeHex("fg", &byte, 1u));

    Context invalid;
    CHECK(!Parse(wire.substr(0, kHeaderSize - 1u), invalid));
    CHECK(!Parse(wire.substr(0, kHeaderSize), invalid));
    CHECK(!Parse(wire + "x", invalid));
    return 0;
}
