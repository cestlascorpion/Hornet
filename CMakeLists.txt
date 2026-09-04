cmake_minimum_required(VERSION 3.16)

project(Hornet LANGUAGES CXX)

find_package(Threads REQUIRED)

set(PROJECT_WARNINGS
    -Wall
    -Wextra
    -Wconversion
    -Wsizeof-pointer-memaccess
    -Wfloat-equal
    -Wconversion-null
    -Woverflow
    -Wshadow)
set(HORNET_TRACE_LIBRARIES
    opentelemetry_exporter_zipkin_trace
    opentelemetry_exporter_jaeger_trace
    opentelemetry_http_client_curl
    thrift
    opentelemetry_common
    curl
    opentelemetry_exporter_ostream_span
    opentelemetry_trace
    opentelemetry_resources
    opentelemetry_version
    yaml-cpp)

file(GLOB_RECURSE HORNET_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HORNET_HEADERS CONFIGURE_DEPENDS src/*.h)

add_library(HornetBinary STATIC src/Binary.cpp src/Binary.h)
set_target_properties(HornetBinary PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_definitions(HornetBinary PRIVATE _FILE_OFFSET_BITS=64 _GNU_SOURCE)
target_compile_features(HornetBinary PUBLIC cxx_std_11)
target_compile_options(HornetBinary PRIVATE ${PROJECT_WARNINGS} -g -O0)
target_include_directories(HornetBinary PUBLIC src)
target_link_libraries(HornetBinary PUBLIC Threads::Threads)

list(FILTER HORNET_SOURCES EXCLUDE REGEX "Binary\\.cpp$")
add_library(Hornet STATIC ${HORNET_SOURCES} ${HORNET_HEADERS})
set_target_properties(Hornet PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_definitions(Hornet PRIVATE _FILE_OFFSET_BITS=64 _GNU_SOURCE)
target_compile_features(Hornet PUBLIC cxx_std_11)
target_compile_options(Hornet PRIVATE ${PROJECT_WARNINGS} -g -O0)
target_include_directories(Hornet PUBLIC src)
target_link_libraries(Hornet PUBLIC HornetBinary Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_executable(HornetBinaryTest test/Binary.cpp)
    target_compile_options(HornetBinaryTest PRIVATE ${PROJECT_WARNINGS} -g -O0)
    target_link_libraries(HornetBinaryTest PRIVATE HornetBinary)
    add_test(NAME HornetBinaryTest COMMAND HornetBinaryTest)
endif()

add_executable(Trace test/Trace.cpp)
target_compile_options(Trace PRIVATE ${PROJECT_WARNINGS} -g -O0)
target_link_libraries(Trace PRIVATE Hornet ${HORNET_TRACE_LIBRARIES})
