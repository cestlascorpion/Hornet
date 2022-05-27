#include "Tracing.h"

#define ENABLE_RLOG

#include <chrono>
#include <iostream>
#include <thread>

#ifdef ENABLE_RLOG
#include <rlog/rlog.h>
#endif

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
constexpr const unsigned cmd = 10u;
constexpr const unsigned uid = 12345678u;

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

void F2() {
    auto ctx = Tracing::Instance()->StartSpan("", "test", "F1", SpanKind::kServer);
    this_thread::sleep_for(chrono::milliseconds(10));

    auto ret = Tracing::ParseFromJaegerContext(Tracing::GetJaegerContext());
    cout << "->f2:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;
    Tracing::Instance()->EndSpan(move(ctx), 0);
}

void F1(const string &remote) {
    auto ctx = Tracing::Instance()->StartSpan(remote, "test", "F0", SpanKind::kClient, uid, cmd, remote.empty());
    auto ret = Tracing::ParseFromJaegerContext(Tracing::GetJaegerContext());
    cout << "f1->:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;

    F2();
    this_thread::sleep_for(chrono::milliseconds(10));

    ret = Tracing::ParseFromJaegerContext(Tracing::GetJaegerContext());
    cout << "->f1:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;
    Tracing::Instance()->EndSpan(move(ctx), 0);
}

void F3() {
    auto ctx = Tracing::Instance()->StartIsolatedSpan("", "test", "F3", SpanKind::kClient, uid, cmd, true);
    this_thread::sleep_for(chrono::milliseconds(10));
    Tracing::Instance()->EndIsolatedSpan(move(ctx), 0);
}

int main() {
#ifdef ENABLE_RLOG
    LOG_INIT(".", "tester", LOG_LEVEL::TRACE);
#endif

    char buffer[strlen(hexParentContext) / 2];
    if (!HexToBinary(hexParentContext, (uint8_t *)buffer, sizeof(buffer))) {
        cout << "invalid parent context" << endl;
        return 0;
    }

    cout << "----------------------------------------" << endl;
    auto ctx = Tracing::ParseFromJaegerContext(string(buffer, sizeof(buffer)));
    cout << "f0:" << ctx._traceId << "-" << ctx._spanId << "-" << ctx._parentSpanId << "-" << ctx._sampled << endl;
    for (const auto &item : ctx._baggage) {
        cout << "\t" << item.first << ": " << item.second << endl;
    }

    {
        auto jtx = Tracing::FormatAsJaegerContext(ctx);
        if (strcmp(buffer, jtx.c_str()) != 0) {
            cout << "FormatAsJaegerContext go wrong" << endl;
            return 0;
        }
        auto ptx = Tracing::ParseFromJaegerContext(jtx);
        cout << "p0:" << ptx._traceId << "-" << ptx._spanId << "-" << ptx._parentSpanId << "-" << ptx._sampled << endl;
        for (const auto &item : ptx._baggage) {
            cout << "\t" << item.first << ": " << item.second << endl;
        }
    }

    cout << "----------------------------------------" << endl;

    for (auto i = 0; i < 30; i++) {
        F1(string(buffer, sizeof(buffer)));
        this_thread::sleep_for(chrono::seconds(2));
    }

    cout << "----------------------------------------" << endl;

    for (auto i = 0; i < 100; i++) {
        F1("");
        this_thread::sleep_for(chrono::seconds(2));
    }
    cout << "----------------------------------------" << endl;

    for (auto i = 0; i < 100; i++) {
        F3();
        this_thread::sleep_for(chrono::seconds(2));
    }

    this_thread::sleep_for(chrono::milliseconds(1000));

    return 0;
}