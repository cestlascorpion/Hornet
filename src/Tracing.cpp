#include "Tracing.h"
#include "Common.h"
#include "LogHandler.h"
#include "Propagator.h"
#include "Sampler.h"

#include <opentelemetry/context/propagation/global_propagator.h>
#ifdef JAEGER_EXPORTER
#include <opentelemetry/exporters/jaeger/jaeger_exporter.h>
#else
#include <opentelemetry/exporters/zipkin/zipkin_exporter.h>
#endif
#ifdef OSTREAM_EXPORTER_DEBUG
#include <opentelemetry/exporters/ostream/span_exporter.h>
#endif
#include <opentelemetry/sdk/common/global_log_handler.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor.h>
#include <opentelemetry/sdk/trace/samplers/parent.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/provider.h>

#include <yaml-cpp/yaml.h>

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

Scope::Scope(nostd::shared_ptr<trace::Span> span, unique_ptr<context::Token> token)
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

IsolatedScope::IsolatedScope()
    : _ctx()
    , _span(nullptr) {}

IsolatedScope::IsolatedScope(string ctx, nostd::shared_ptr<trace::Span> span)
    : _ctx(move(ctx))
    , _span(move(span)) {}

IsolatedScope::~IsolatedScope() = default;

IsolatedScope::IsolatedScope(IsolatedScope &&isc) noexcept
    : _ctx(move(isc._ctx))
    , _span(move(isc._span)) {}

IsolatedScope &IsolatedScope::operator=(IsolatedScope &&isc) noexcept {
    if (this != &isc) {
        _ctx = move(isc._ctx);
        _span = move(isc._span);
    }
    return *this;
}

string IsolatedScope::GetContext() noexcept {
    return _ctx;
}

string IsolatedScope::GetTraceId() noexcept {
    char trace[32];
    _span->GetContext().trace_id().ToLowerBase16(trace);
    return string(trace, sizeof(trace));
}

void IsolatedScope::SetAttr(nostd::string_view key, const common::AttributeValue &value) noexcept {
    if (_span != nullptr) {
        _span->SetAttribute(key, value);
    }
}

struct Tracing::TraceConf {
    TraceConf()
        : _logSpan(false)
#ifdef JAEGER_EXPORTER
        , _address("localhost:6831") {
#else
        , _address("http://localhost:9411/api/v2/spans") {
#endif
        const char *path = getenv(k_DefaultPathEnv);
        if (path == nullptr || strlen(path) == 0) {
            path = k_DefaultPath;
        }

        int ret = access(path, F_OK);
        if (ret != 0) {
            return;
        }

        auto config = YAML::LoadFile(path);
        if (config.IsNull() || !config.IsMap()) {
            return;
        }

        auto reporter = config["reporter"];
        if (reporter.IsNull() || !reporter.IsMap()) {
            return;
        }

        auto logSpans = reporter["logSpans"];
        if (!logSpans.IsNull() && logSpans.IsScalar()) {
            _logSpan = logSpans.as<bool>();
        }
#ifdef JAEGER_EXPORTER
        auto jaegerEndpoint = reporter["jaegerEndpoint"];
        if (!jaegerEndpoint.IsNull() && jaegerEndpoint.IsScalar()) {
            _address = jaegerEndpoint.as<string>();
        }
#else
        auto zipkinEndpoint = reporter["zipkinEndpoint"];
        if (!zipkinEndpoint.IsNull() && zipkinEndpoint.IsScalar()) {
            _address = zipkinEndpoint.as<string>();
        }
#endif
    }

    bool _logSpan;
    string _address;
};

Tracing::Tracing()
    : _conf(new TraceConf) {
    auto lh = nostd::shared_ptr<sdk::common::internal_log::LogHandler>(new CustomLogHandler());
    sdk::common::internal_log::GlobalLogHandler::SetLogHandler(move(lh));
    sdk::common::internal_log::GlobalLogHandler::SetLogLevel(
        _conf->_logSpan ? sdk::common::internal_log::LogLevel::Debug : sdk::common::internal_log::LogLevel::Info);
#ifdef JAEGER_EXPORTER
    exporter::jaeger::JaegerExporterOptions exOpts{};
    auto pos = _conf->_address.find(':');
    exOpts.endpoint = _conf->_address.substr(0, pos);
    exOpts.server_port = (uint16_t)atoi(_conf->_address.substr(pos + 1).c_str());
    auto e = unique_ptr<sdk::trace::SpanExporter>(new exporter::jaeger::JaegerExporter(exOpts));
#else
    exporter::zipkin::ZipkinExporterOptions exOpts{};
    exOpts.endpoint = _conf->_address;
    exOpts.service_name = detail::GetProcName();
    auto e = unique_ptr<sdk::trace::SpanExporter>(new exporter::zipkin::ZipkinExporter());
#endif

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
                         unsigned int uid, unsigned int cmd, bool root) noexcept {
    stringstream op;
    if (proc.empty()) {
        op << "proc";
    } else {
        op << proc;
    }

    auto svr = op.str(); // proc name
    transform(svr.begin(), svr.end(), svr.begin(), ::tolower);
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
        carrier.Set(jaeger::kBinaryFormat, context);
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
    if (_conf->_logSpan) {
        // TODO
    }
    return Scope{move(span), move(token)};
}

void Tracing::EndSpan(Scope context, int err, opentelemetry::nostd::string_view msg) noexcept {
    if (context._span != nullptr && context._token != nullptr) {
        context._span->SetAttribute(kTraceTagErr, err);
        context._span->SetStatus(err == 0 ? trace::StatusCode::kOk : trace::StatusCode::kError, msg);
        if (_conf->_logSpan) {
            // TODO
        }
        context._span->End();
    }
}

IsolatedScope Tracing::StartIsolatedSpan(const string &context, const string &proc, const string &func,
                                         trace::SpanKind kind, unsigned int uid, unsigned int cmd, bool root) noexcept {
    stringstream op;
    if (proc.empty()) {
        op << "proc";
    } else {
        op << proc;
    }

    auto svr = op.str(); // proc name
    transform(svr.begin(), svr.end(), svr.begin(), ::tolower);
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
        carrier.Set(jaeger::kBinaryFormat, context);
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
    auto pr = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto ctx = context::RuntimeContext::GetCurrent();
    CustomCarrier carrier;
    pr->Inject(carrier, ctx);

    auto tc = carrier.Get(jaeger::kBinaryFormat);
    if (_conf->_logSpan) {
        // TODO
    }
    return IsolatedScope{string(tc.data(), tc.size()), move(span)};
}

void Tracing::EndIsolatedSpan(IsolatedScope context, int err, opentelemetry::nostd::string_view msg) noexcept {
    if (context._span != nullptr) {
        context._span->SetAttribute(kTraceTagErr, err);
        context._span->SetStatus(err == 0 ? trace::StatusCode::kOk : trace::StatusCode::kError, msg);
        if (_conf->_logSpan) {
            // TODO
        }
        context._span->End();
    }
}

} // namespace tracing