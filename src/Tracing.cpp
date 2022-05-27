#include "Tracing.h"
#include "Common.h"
#include "Propagator.h"
#include "Sampler.h"

// #define OSTREAM_EXPORTER_DEBUG
#define ENABLE_RLOG

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
#ifdef ENABLE_RLOG
#include <rlog/rlog.h>
#endif
#include <unistd.h>
#include <yaml-cpp/yaml.h>

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

IsolatedScope::IsolatedScope()
    : _ctx()
    , _span(nullptr) {}

IsolatedScope::IsolatedScope(string &&ctx, nostd::shared_ptr<trace::Span> &&span)
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

void IsolatedScope::SetAttr(nostd::string_view key, const common::AttributeValue &value) noexcept {
    if (_span != nullptr) {
        _span->SetAttribute(key, value);
    }
}

struct Tracing::TraceConf {
    TraceConf()
        : _host("127.0.0.1")
        , _port(6831)
        , _logSpan(false) {
        const char *path = getenv(k_DefaultPathEnv);
        if (path == nullptr || strlen(path) == 0) {
#ifdef ENABLE_RLOG
            LOG_WARN("k_DefaultPathEnv not found, use default path %s", k_DefaultPath);
#endif
            path = k_DefaultPath;
        }

        int ret = access(path, F_OK);
        if (ret != 0) {
#ifdef ENABLE_RLOG
            LOG_WARN("path %s not exist, please check it", path);
#endif
            return;
        }

        auto config = YAML::LoadFile(path);
        if (config.IsNull() || !config.IsMap()) {
#ifdef ENABLE_RLOG
            LOG_WARN("load root failed, use default addr %s:%u log %d", _host.c_str(), _port, _logSpan);
#endif
            return;
        }

        auto reporter = config["reporter"];
        if (reporter.IsNull() || !reporter.IsMap()) {
#ifdef ENABLE_RLOG
            LOG_WARN("load reporter failed, use default addr %s:%u log %d", _host.c_str(), _port, _logSpan);
#endif
            return;
        }

        auto logSpans = reporter["logSpans"];
        if (!logSpans.IsNull() && logSpans.IsScalar()) {
            _logSpan = logSpans.as<bool>();
        }
        auto localAgentHostPort = reporter["localAgentHostPort"];
        if (!localAgentHostPort.IsNull() && localAgentHostPort.IsScalar()) {
            auto hostPort = localAgentHostPort.as<string>();
            auto pos = hostPort.find(":");
            if (pos != string::npos) {
                _host = hostPort.substr(0, pos);
                _port = (uint16_t)atoi(hostPort.substr(pos + 1).c_str());
            }
        }
#ifdef ENABLE_RLOG
        LOG_INFO("load config success, addr %s:%u log %d", _host.c_str(), _port, _logSpan);
#endif
    }

    string _host;
    uint16_t _port;
    bool _logSpan;
};

Tracing::Tracing()
    : _conf(new TraceConf) {

    exporter::jaeger::JaegerExporterOptions exOpts{};
    exOpts.endpoint = _conf->_host;
    exOpts.server_port = _conf->_port;
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
#ifdef ENABLE_RLOG
        LOG_DEBUG("trace id %s span id %s sampled %d", FormatTraceId(span->GetContext().trace_id()).c_str(),
                  FormatSpanId(span->GetContext().span_id()).c_str(), span->GetContext().IsSampled());
#endif
    }
    return Scope{move(span), move(token)};
}

void Tracing::EndSpan(Scope &&context, int err, opentelemetry::nostd::string_view msg) noexcept {
    if (context._span != nullptr && context._token != nullptr) {
        context._span->SetAttribute(kTraceTagErr, err);
        context._span->SetStatus(err == 0 ? trace::StatusCode::kOk : trace::StatusCode::kError, msg);
        if (_conf->_logSpan) {
#ifdef ENABLE_RLOG
            LOG_DEBUG(
                "trace id %s span id %s sampled %d", FormatTraceId(context._span->GetContext().trace_id()).c_str(),
                FormatSpanId(context._span->GetContext().span_id()).c_str(), context._span->GetContext().IsSampled());
#endif
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
#ifdef ENABLE_RLOG
        LOG_DEBUG("trace id %s span id %s sampled %d", FormatTraceId(span->GetContext().trace_id()).c_str(),
                  FormatSpanId(span->GetContext().span_id()).c_str(), span->GetContext().IsSampled());
#endif
    }
    return IsolatedScope{string(tc.data(), tc.size()), move(span)};
}

void Tracing::EndIsolatedSpan(IsolatedScope &&context, int err, opentelemetry::nostd::string_view msg) noexcept {
    if (context._span != nullptr) {
        context._span->SetAttribute(kTraceTagErr, err);
        context._span->SetStatus(err == 0 ? trace::StatusCode::kOk : trace::StatusCode::kError, msg);
        if (_conf->_logSpan) {
#ifdef ENABLE_RLOG
            LOG_DEBUG(
                "trace id %s span id %s sampled %d", FormatTraceId(context._span->GetContext().trace_id()).c_str(),
                FormatSpanId(context._span->GetContext().span_id()).c_str(), context._span->GetContext().IsSampled());
#endif
        }
        context._span->End();
    }
}

} // namespace tracing