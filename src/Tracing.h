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

struct Context {
    explicit Context(const std::string &context);

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
    Scope StartSpan(const std::string &context,  // remote context (jaeger binary context, can be empty if root == true)
                    const std::string &proc,     // proc name
                    const std::string &func,     // func name
                    SpanKind kind,               // span kind
                    unsigned uid = 0,            // user id
                    unsigned cmd = 0,            // command id
                    bool root = false) noexcept; // root of trace
    // EndSpan: end span with the given scope (from StartSpan())
    void EndSpan(Scope &&context, int err = 0) noexcept;

public:
    // CurrentContext: get current active context in jaeger binary format
    static std::string CurrentContext() noexcept;
    // ParseContext: parse context of jaeger binary format
    static Context ParseContext(const std::string &context) noexcept;

private:
    Tracing();

private:
    struct TraceConf;
    std::unique_ptr<TraceConf> _conf;
};

} // namespace tracing