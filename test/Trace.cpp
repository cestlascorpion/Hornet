#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include "Binary.h"
#include "Tracing.h"

using namespace std;
using namespace Tracing;
using namespace opentelemetry;

using Tracer = Tracing::Tracing;

constexpr const char *hexParentContext =
    "FEA80376EE0C6F9FEC9C673F09F6EAE1" // trace id
    "9E5304AE5F1682BB"                 // span id
    "0000000000000000"                 // parent span id
    "01"                               // flags
    "00000001"                         // baggage count
    "00000008"                         // key length
    "70796A5F74657374"                 // pyj_test
    "0000000B"                         // value length
    "68656C6C6F20776F726C64";          // hello world

constexpr const unsigned cmd = 10u;
constexpr const unsigned uid = 12345678u;

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
    string buffer(strlen(hexParentContext) / 2u, '\0');
    if (!Tracing::binary::DecodeHex(hexParentContext, reinterpret_cast<uint8_t *>(&buffer[0]), buffer.size())) {
        cout << "invalid parent context" << endl;
        return 1;
    }

    cout << "----------------------------------------" << endl;
    auto ctx = Tracer::ParseFromJaegerContext(buffer);
    cout << "f0:" << ctx._traceId << "-" << ctx._spanId << "-" << ctx._parentSpanId << "-" << ctx._sampled << endl;
    for (const auto &item : ctx._baggage) {
        cout << "\t" << item.first << ": " << item.second << endl;
    }

    {
        auto jtx = Tracer::FormatAsJaegerContext(ctx);
        if (jtx.size() != buffer.size() || memcmp(buffer.data(), jtx.data(), buffer.size()) != 0) {
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

    if (!F1(buffer)) {
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
