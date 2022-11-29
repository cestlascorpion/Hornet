#include "Tracing.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std;
using namespace tracing;
using namespace opentelemetry;

constexpr const char *hexParentContext = "FEA80376EE0C6F9F"
                                         "EC9C673F09F6EAE1" // 9f6f0cee7603a8fee1eaf6093f679cec
                                         "9E5304AE5F1682BB" // bb82165fae04539e
                                         "0000000000000000" // 000000000
                                         "01000000"         // true
                                         "01000000"
                                         "0870796A5F746573740000000B68656C6C6F20776F726C64";

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

    F1(string(buffer, sizeof(buffer)));
    this_thread::sleep_for(chrono::seconds(2));

    cout << "----------------------------------------" << endl;

    F1("");
    this_thread::sleep_for(chrono::seconds(2));

    cout << "----------------------------------------" << endl;
    F3();
    this_thread::sleep_for(chrono::seconds(2));

    return 0;
}