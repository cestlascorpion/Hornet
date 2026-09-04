cmake_minimum_required(VERSION 3.16)
project(Hornet)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wconversion -Wsizeof-pointer-memaccess \
                     -Wfloat-equal -Wconversion-null -Woverflow -Wshadow \
                     -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -pthread -g -O0")

file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HDRS CONFIGURE_DEPENDS src/*.h)

add_library(HornetBinary STATIC src/Binary.cpp src/Binary.h)
target_include_directories(HornetBinary PUBLIC src)

list(FILTER SRCS EXCLUDE REGEX "Binary\\.cpp$")
add_library(Hornet STATIC ${SRCS} ${HDRS})
target_include_directories(Hornet PUBLIC src)
target_link_libraries(Hornet PUBLIC HornetBinary)

include(CTest)
if (BUILD_TESTING)
    add_executable(HornetBinaryTest test/Binary.cpp)
    target_link_libraries(HornetBinaryTest PRIVATE HornetBinary)
    add_test(NAME HornetBinaryTest COMMAND HornetBinaryTest)
endif ()

foreach (_target
        Trace)
    add_executable(${_target} "test/${_target}.cpp")
    target_link_libraries(${_target} PRIVATE
            Hornet
            opentelemetry_exporter_zipkin_trace
            opentelemetry_exporter_jaeger_trace
            opentelemetry_http_client_curl
            thrift
            opentelemetry_common
            curl
            opentelemetry_exporter_ostream_span
            opentelemetry_trace
            opentelemetry_common
            opentelemetry_resources
            opentelemetry_version
            yaml-cpp)
endforeach ()
