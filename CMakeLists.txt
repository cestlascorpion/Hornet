cmake_minimum_required(VERSION 3.16)
project(Hornet)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wconversion -Wsizeof-pointer-memaccess \
                     -Wfloat-equal -Wconversion-null -Woverflow -Wshadow \
                     -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -pthread -g -O0")

include_directories(
        src
        test)

file(GLOB_RECURSE SRCS src/*.cpp src/*.cpp)
file(GLOB_RECURSE HDRS src/*.h src/*.h)

add_library(Hornet STATIC ${SRCS} ${HDRS})

foreach (_target
        Trace)
    add_executable(${_target} "test/${_target}.cpp")
    target_link_libraries(${_target}
            ${PROJECT_BINARY_DIR}/libHornet.a
            opentelemetry_exporter_jaeger_trace
            http_client_curl
            thrift
            opentelemetry_common
            curl
            opentelemetry_exporter_ostream_span
            opentelemetry_trace
            opentelemetry_common
            opentelemetry_resources
            opentelemetry_version)
endforeach ()