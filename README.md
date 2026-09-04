# Hornet

Hornet is an experimental C++11 OpenTelemetry tracing demo with Jaeger binary context propagation, sampling, and Zipkin or Jaeger exporters. It is not a production tracing library.

## Requirements

- CMake 3.16 or later
- A C++11 compiler
- OpenTelemetry C++, yaml-cpp, curl, and thrift libraries

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

The `Trace` executable uses `TRACING_CTRL_CONF` when set, otherwise `/etc/conf/tracing.yml`. See [BUG.md](BUG.md) for the historical issue analysis and remaining sampling hot-reload risk.
