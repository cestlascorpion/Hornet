#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "Tracing.h"

using namespace std;
using namespace Tracing;
using namespace opentelemetry;

using Tracer = Tracing::Tracing;

constexpr const char *hexParentContext =
    "FEA80376EE0C6F9F"
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

bool SameContext(const Context &left, const Context &right) {
    return left._traceId == right._traceId && left._spanId == right._spanId && left._sampled == right._sampled;
}

void F2() {
    auto ctx = Tracer::Instance()->StartSpan("", "test", "F1", SpanKind::kServer);
    this_thread::sleep_for(chrono::milliseconds(10));

    auto ret = Tracer::ParseFromJaegerContext(Tracer::GetJaegerContext());
    cout << "->f2:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;
    Tracer::Instance()->EndSpan(move(ctx), 0);
}

bool F1(const string &remote) {
    auto ctx = Tracer::Instance()->StartSpan(remote, "test", "F0", SpanKind::kClient, uid, cmd, remote.empty());
    auto ret = Tracer::ParseFromJaegerContext(Tracer::GetJaegerContext());
    cout << "f1->:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;
    auto before = ret;

    F2();
    this_thread::sleep_for(chrono::milliseconds(10));

    ret = Tracer::ParseFromJaegerContext(Tracer::GetJaegerContext());
    cout << "->f1:" << ret._traceId << "-" << ret._spanId << "-" << ret._parentSpanId << "-" << ret._sampled << endl;
    auto restored = SameContext(before, Tracer::ParseFromJaegerContext(Tracer::GetJaegerContext()));
    Tracer::Instance()->EndSpan(move(ctx), 0);
    return restored;
}

bool F3() {
    auto before = Tracer::GetPlainTextContext();
    auto ctx = Tracer::Instance()->StartIsolatedSpan("", "test", "F3", SpanKind::kClient, uid, cmd, true);
    auto preserved = SameContext(before, Tracer::GetPlainTextContext());
    this_thread::sleep_for(chrono::milliseconds(10));
    Tracer::Instance()->EndIsolatedSpan(move(ctx), 0);
    return preserved && SameContext(before, Tracer::GetPlainTextContext());
}

int main() {
    char buffer[strlen(hexParentContext) / 2];
    if (!HexToBinary(hexParentContext, (uint8_t *)buffer, sizeof(buffer))) {
        cout << "invalid parent context" << endl;
        return 0;
    }

    cout << "----------------------------------------" << endl;
    auto ctx = Tracer::ParseFromJaegerContext(string(buffer, sizeof(buffer)));
    cout << "f0:" << ctx._traceId << "-" << ctx._spanId << "-" << ctx._parentSpanId << "-" << ctx._sampled << endl;
    for (const auto &item : ctx._baggage) {
        cout << "\t" << item.first << ": " << item.second << endl;
    }

    {
        auto jtx = Tracer::FormatAsJaegerContext(ctx);
        if (jtx.size() != sizeof(buffer) || memcmp(buffer, jtx.data(), sizeof(buffer)) != 0) {
            cout << "FormatAsJaegerContext go wrong" << endl;
            return 1;
        }
        auto ptx = Tracer::ParseFromJaegerContext(jtx);
        cout << "p0:" << ptx._traceId << "-" << ptx._spanId << "-" << ptx._parentSpanId << "-" << ptx._sampled << endl;
        for (const auto &item : ptx._baggage) {
            cout << "\t" << item.first << ": " << item.second << endl;
        }
    }

    cout << "----------------------------------------" << endl;

    if (!F1(string(buffer, sizeof(buffer)))) {
        return 1;
    }
    this_thread::sleep_for(chrono::seconds(2));

    cout << "----------------------------------------" << endl;

    if (!F1("")) {
        return 1;
    }
    this_thread::sleep_for(chrono::seconds(2));

    cout << "----------------------------------------" << endl;
    if (!F3()) {
        return 1;
    }
    this_thread::sleep_for(chrono::seconds(2));

    return 0;
}
