#pragma once

#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/trace/span.h>

#include <map>

namespace tracing {

using SpanKind = opentelemetry::trace::SpanKind;

struct Scope {
    Scope();
    Scope(opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> &&span,
          std::unique_ptr<opentelemetry::context::Token> &&token);
    ~Scope();

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    Scope(Scope &&) noexcept;
    Scope &operator=(Scope &&) noexcept;

    // 添加属性值: 用于添加 StartSpan 无法获取的属性值
    void SetAttr(opentelemetry::nostd::string_view key, const opentelemetry::common::AttributeValue &value) noexcept;

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
    // 开启 Span: context 在根结点处为空，其余节点为非空；返回值不可丢弃
    Scope StartSpan(const std::string &context, // remote context (jaeger binary context)
                    const std::string &proc,    // proc name
                    const std::string &func,    // func name
                    SpanKind kind,              // span kind
                    unsigned uid = 0,           // user id
                    unsigned cmd = 0,           // command id
                    bool root = false);         // root of trace
    // 结束 Span: 必须配合 StartSpan() 使用
    void EndSpan(Scope &&context, int err = 0);

public:
    // 获取当前 Context： jaeger binary context
    static std::string GetJaegerContext();
    // 解析已知 Context：jaeger binary context
    static Context ParseJaegerContext(const std::string &context);

private:
    Tracing();

private:
    struct TraceConf;
    std::unique_ptr<TraceConf> _conf;
};

} // namespace tracing