#include "Tracing.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std;
using namespace tracing;
using namespace opentelemetry;

constexpr const char *hexParentContext =
    "0000000000000000a03bb80ba85889b2"
    "ebb15cfc5df6613f"
    "a03bb80ba85889b2"
    "310300000"
    "005000000"
    "636f6e676f0b0000007436317263576b674d7a45030000006b65790500000076616c756504000000776861740300000077686f";
constexpr const unsigned cmd = 0u;
constexpr const unsigned uid = 0u;
constexpr const bool rot = true;

constexpr int8_t kHexDigits[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

unsigned char HexToInt(char c) {
    return (unsigned char)kHexDigits[uint8_t(c)];
}

bool HexToBinary(const string &hex, uint8_t *buffer, size_t buffer_size) {
    memset(buffer, 0, buffer_size);
    if (hex.size() > buffer_size * 2) {
        return false;
    }
    auto hex_size = (hex.size());
    auto buffer_pos = buffer_size - (hex_size + 1) / 2;
    auto last_hex_pos = hex_size - 1;
    auto i = 0u;
    for (; i < last_hex_pos; i += 2) {
        buffer[buffer_pos++] = static_cast<uint8_t>((HexToInt(hex[i]) << 4) | HexToInt(hex[i + 1]));
    }
    if (i == last_hex_pos) {
        buffer[buffer_pos] = HexToInt(hex[i]);
    }
    return true;
}

void f2() {
    auto ctx = Tracing::Instance()->StartSpan("", "test", "f1", SpanKind::kServer);
    this_thread::sleep_for(chrono::milliseconds(10));

    auto ret = Tracing::ParseJaegerContext(Tracing::Instance()->GetJaegerContext());
    cout << "->f2:" << ret._traceId << "-" << ret._spanId << "-" << ret._sampled << "-" << ret._parentSpanId << endl;
    Tracing::Instance()->EndSpan(move(ctx), 0);
}

void f1(const string &remote) {
    auto ctx = Tracing::Instance()->StartSpan(remote, "test", "f0", SpanKind::kClient, uid, cmd, rot);
    auto ret = Tracing::ParseJaegerContext(Tracing::Instance()->GetJaegerContext());
    cout << "f1->:" << ret._traceId << "-" << ret._spanId << "-" << ret._sampled << "-" << ret._parentSpanId << endl;

    f2();
    this_thread::sleep_for(chrono::milliseconds(10));

    ret = Tracing::ParseJaegerContext(Tracing::Instance()->GetJaegerContext());
    cout << "->f1:" << ret._traceId << "-" << ret._spanId << "-" << ret._sampled << "-" << ret._parentSpanId << endl;
    Tracing::Instance()->EndSpan(move(ctx), 0);
}

int main() {
    char buffer[strlen(hexParentContext) / 2];
    if (!HexToBinary(hexParentContext, (uint8_t *)buffer, sizeof(buffer))) {
        cout << "invalid parent context" << endl;
        return 0;
    }
    cout << "----------------------------------------" << endl;
    auto ctx = Tracing::ParseJaegerContext(string(buffer, sizeof(buffer)));
    cout << "f0:" << ctx._traceId << "-" << ctx._spanId << "-" << ctx._sampled << "-" << ctx._parentSpanId << endl;
    cout << "----------------------------------------" << endl;
    f1(string(buffer, sizeof(buffer)));
    cout << "----------------------------------------" << endl;
    return 0;
}