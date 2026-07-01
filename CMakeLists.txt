cmake_minimum_required(VERSION 3.14)
project(monitor_system VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_EBPF  "Enable eBPF observe & diagnose" ON)
option(ENABLE_PERF  "Enable perf-triggered diagnose" ON)

add_subdirectory(proto)
add_subdirectory(worker)
add_subdirectory(manager)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
