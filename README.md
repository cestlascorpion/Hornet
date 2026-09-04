# Hornet

Hornet is an experimental C++ OpenTelemetry demo that supports Jaeger's binary
`trace-ctx` propagation format. It is intended for evaluating context
propagation, sampling, and Zipkin or Jaeger exporters. It is not a production
tracing library.

## Features

- Jaeger binary context injection and extraction
- Conversion between binary and plain-text trace contexts
- Configurable root-span sampling with a ratio and user allowlist
- Zipkin exporter by default, with optional Jaeger and ostream exporters
- Normal and isolated span creation

## Requirements

- CMake 3.16 or later
- A C++11 compiler
- OpenTelemetry C++ headers and the libraries listed in `CMakeLists.txt`
- yaml-cpp, curl, thrift, and their transitive dependencies

The project expects these dependencies to be supplied by the active toolchain
or system installation.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The example executable is `build/Trace`.

## Configuration

Set `TRACING_CTRL_CONF` to a YAML configuration file. If it is not set, Hornet
uses `/etc/conf/tracing.yml`.

```yaml
reporter:
  logSpans: true
  zipkinEndpoint: http://localhost:9411/api/v2/spans

sampler:
  ratio: 50
  white-list:
    - 1072744497
```

`ratio` is expressed in ten-thousandths. A value of `10000` samples every root
span and `0` samples none, except for the per-command periodic sample.

## API

The public namespace is `Tracing` and the tracer class is also named
`Tracing`, so its fully qualified name is `Tracing::Tracing`.

```cpp
auto scope = Tracing::Tracing::Instance()->StartSpan(
    remoteContext, "service", "operation", Tracing::SpanKind::kServer);

Tracing::Tracing::Instance()->EndSpan(std::move(scope));
```

Use `StartIsolatedSpan` when a span must be propagated without changing the
active runtime context.
