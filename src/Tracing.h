#pragma once

#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/trace/span.h>

#include <map>

namespace tracing {

using SpanKind = opentelemetry::trace::SpanKind;

struct Scope {
public:
    Scope();
    Scope(opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> &&span,
          std::unique_ptr<opentelemetry::context::Token> &&token);
    ~Scope();

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    Scope(Scope &&) noexcept;
    Scope &operator=(Scope &&) noexcept;

public:
    void SetAttr(opentelemetry::nostd::string_view key, const opentelemetry::common::AttributeValue &value) noexcept;

private:
    friend class Tracing;
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> _span; // current span
    std::unique_ptr<opentelemetry::context::Token> _token; // scope which controls the life circle of the span
};

struct IsolatedScope {
public:
    IsolatedScope();
    IsolatedScope(std::string &&ctx, opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> &&span);
    ~IsolatedScope();

    IsolatedScope(const IsolatedScope &) = delete;
    IsolatedScope &operator=(const IsolatedScope &) = delete;

    IsolatedScope(IsolatedScope &&) noexcept;
    IsolatedScope &operator=(IsolatedScope &&) noexcept;

public:
    void SetAttr(opentelemetry::nostd::string_view key, const opentelemetry::common::AttributeValue &value) noexcept;

private:
    friend class Tracing;
    std::string _ctx;                                                   // isolated context
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> _span; // current span
};

struct Context {
    explicit Context(const std::string &context);
    explicit Context(const opentelemetry::trace::SpanContext &context);
    Context(const std::string &traceId, const std::string &spanId, const std::string &parentSpanId, bool sampled,
            const std::map<std::string, std::string> &baggage = std::map<std::string, std::string>());

    std::string _traceId;
    std::string _spanId;
    std::string _parentSpanId; // nothing
    bool _sampled;
    std::map<std::string, std::string> _baggage;
};

class Tracing final {
public:
    static Tracing *Instance();

    ~Tracing();

public:
    // StartSpan: create a new span
    Scope StartSpan(const std::string &context,  // remote context (jaeger binary context)
                    const std::string &proc,     // proc name
                    const std::string &func,     // func name
                    SpanKind kind,               // span kind
                    unsigned uid = 0,            // user id
                    unsigned cmd = 0,            // command id
                    bool root = false) noexcept; // root of trace
    // EndSpan: end span with the given scope (from StartSpan())
    void EndSpan(Scope &&context, int err = 0, opentelemetry::nostd::string_view msg = "") noexcept;

public:
    // StartIsolatedSpan: create a new span without setting "active"
    IsolatedScope StartIsolatedSpan(const std::string &context,  // remote context (jaeger binary context)
                                    const std::string &proc,     // proc name
                                    const std::string &func,     // func name
                                    SpanKind kind,               // span kind
                                    unsigned uid = 0,            // user id
                                    unsigned cmd = 0,            // command id
                                    bool root = false) noexcept; // root of trace
    // EndIsolatedSpan: end span with the given scope (from StartIsolatedSpan())
    void EndIsolatedSpan(IsolatedScope &&context, int err = 0, opentelemetry::nostd::string_view msg = "") noexcept;

public:
    // GetPlainTextContext: get current active context(plaintext format)
    static Context GetPlainTextContext() noexcept;
    // GetJaegerContext: get current active context(jaeger binary format)
    static std::string GetJaegerContext() noexcept;
    // ParseFromJaegerContext: parse jaeger binary format context into plaintext context
    static Context ParseFromJaegerContext(const std::string &context) noexcept;
    // FormatAsJaegerContext: format plaintext context into jaeger binary format context
    static std::string FormatAsJaegerContext(const Context &context) noexcept;

private:
    Tracing();

private:
    struct TraceConf;
    std::unique_ptr<TraceConf> _conf;
};

} // namespace tracing