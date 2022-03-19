#include "Tracing.h"
#include "Common.h"
#include "Propagator.h"
#include "Sampler.h"

#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/exporters/jaeger/jaeger_exporter.h>
#ifdef OSTREAM_EXPORTER_DEBUG
#include <opentelemetry/exporters/ostream/span_exporter.h>
#endif
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor.h>
#include <opentelemetry/sdk/trace/samplers/parent.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/provider.h>

#include <unistd.h>

using namespace std;
using namespace opentelemetry;

namespace detail {

string ProcName() {
    string proc;
    char lsPath[1024] = {0};
    if (readlink("/proc/self/exe", lsPath, sizeof(lsPath)) > 0) {
        char *path_end;
        path_end = strrchr(lsPath, '/');
        if (path_end != nullptr) {
            ++path_end;
            proc = path_end;
        }
    }
    return proc;
}

const string &GetProcName() {
    static const string name(ProcName());
    return name;
}

} // namespace detail

namespace tracing {

Scope::Scope()
    : _span(nullptr)
    , _token(nullptr) {}

Scope::Scope(nostd::shared_ptr<trace::Span> &&span, unique_ptr<context::Token> &&token)
    : _span(move(span))
    , _token(move(token)) {}

Scope::~Scope() = default;

Scope::Scope(Scope &&sc) noexcept
    : _span(move(sc._span))
    , _token(move(sc._token)) {}

Scope &Scope::operator=(Scope &&sc) noexcept {
    if (this != &sc) {
        _span = move(sc._span);
        _token = move(sc._token);
    }
    return *this;
}

void Scope::SetAttr(nostd::string_view key, const common::AttributeValue &value) noexcept {
    if (_span != nullptr) {
        _span->SetAttribute(key, value);
    }
}

struct Tracing::TraceConf {
    TraceConf()
        : _addr("localhost")
        , _port(6831) {
        const char *path = getenv(kTraceConfEnv);
        if (path == nullptr || strlen(path) == 0) {
            path = kTraceConfPath;
        }
        // TODO: read configure file
    }

    string _addr;
    unsigned _port;
};

Tracing::Tracing()
    : _conf(new TraceConf) {

    exporter::jaeger::JaegerExporterOptions exOpts{};
    exOpts.endpoint = _conf->_addr;
    exOpts.server_port = (uint16_t)_conf->_port;
    auto e = unique_ptr<sdk::trace::SpanExporter>(new exporter::jaeger::JaegerExporter(exOpts));

    auto prOpts = sdk::trace::BatchSpanProcessorOptions{};
#ifdef OSTREAM_EXPORTER_DEBUG
    auto p1 = unique_ptr<sdk::trace::SpanProcessor>(new sdk::trace::BatchSpanProcessor(move(e), prOpts));
    auto e2 = unique_ptr<sdk::trace::SpanExporter>(new exporter::trace::OStreamSpanExporter);
    auto p2 = unique_ptr<sdk::trace::SpanProcessor>(new sdk::trace::BatchSpanProcessor(move(e2), prOpts));
#else
    auto p = unique_ptr<sdk::trace::SpanProcessor>(new sdk::trace::BatchSpanProcessor(move(e), prOpts));
#endif
    auto rootSampler = shared_ptr<sdk::trace::Sampler>(new CustomSampler);
    auto s = unique_ptr<sdk::trace::Sampler>(new sdk::trace::ParentBasedSampler(move(rootSampler)));

    auto attr = sdk::resource::ResourceAttributes();
    attr.SetAttribute("service.name", detail::GetProcName());
    auto r = sdk::resource::Resource::Create(attr);
#ifdef OSTREAM_EXPORTER_DEBUG
    vector<unique_ptr<sdk::trace::SpanProcessor>> ps;
    ps.emplace_back(move(p1));
    ps.emplace_back(move(p2));
    auto pv = nostd::shared_ptr<trace::TracerProvider>(new sdk::trace::TracerProvider(move(ps), r, move(s)));
#else
    auto pv = nostd::shared_ptr<trace::TracerProvider>(new sdk::trace::TracerProvider(move(p), r, move(s)));
#endif

    trace::Provider::SetTracerProvider(pv);

    auto pr = nostd::shared_ptr<context::propagation::TextMapPropagator>(new CustomPropagator);
    context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(pr);
}

Tracing::~Tracing() = default;

Tracing *Tracing::Instance() {
    static Tracing instance;
    return &instance;
}

Scope Tracing::StartSpan(const string &context, const string &proc, const string &func, trace::SpanKind kind,
                         unsigned int uid, unsigned int cmd, bool root) {
    stringstream op;
    if (proc.empty()) {
        op << "proc";
    } else {
        op << proc;
    }

    auto svr = op.str(); // proc name
    auto pv = trace::Provider::GetTracerProvider();
    auto tr = pv->GetTracer(svr.c_str(), OPENTELEMETRY_SDK_VERSION);

    if (func.empty()) {
        op << ".func";
    } else {
        op << "." << func;
    }

    trace::StartSpanOptions spOpts;
    spOpts.kind = kind;
    if (!context.empty()) {
        CustomCarrier carrier;
        carrier.Set(kTraceContextOfJaegerBinaryFormat, context);
        auto ctx = context::RuntimeContext::GetCurrent();
        auto pr = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
        spOpts.parent = pr->Extract(carrier, ctx); // carrier -> ctx
    }

    map<nostd::string_view, common::AttributeValue> extra;
    if (uid > 0) {
        extra.emplace(kTraceTagUid, uid);
    }
    if (cmd > 0) {
        extra.emplace(kTraceTagCmd, cmd);
    }
    if (root) {
        extra.emplace(kTraceTagRot, true);
    }
    auto name = op.str(); // proc.func name
    auto span = tr->StartSpan(name.c_str(), extra, spOpts);
    auto token = context::RuntimeContext::Attach(context::RuntimeContext::GetCurrent().SetValue(trace::kSpanKey, span));
    return Scope{move(span), move(token)};
}

void Tracing::EndSpan(Scope &&context, int err) {
    if (context._span != nullptr && context._token != nullptr) {
        context._span->SetAttribute(kTraceTagErr, err);
        context._span->SetStatus(trace::StatusCode::kOk);
        context._span->End();
    }
}

Context Tracing::ParseJaegerContext(const std::string &context) {
    return Context(context);
}

string Tracing::GetJaegerContext() {
    auto pr = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto ctx = context::RuntimeContext::GetCurrent();
    CustomCarrier carrier;
    pr->Inject(carrier, ctx);

    auto tc = carrier.Get(kTraceContextOfJaegerBinaryFormat);
    return {tc.data(), tc.size()};
}

} // namespace tracing