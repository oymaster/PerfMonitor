find_package(nlohmann_json REQUIRED)

set(WORKER_SOURCES
    src/main.cpp
    # monitor
    src/monitor/cpu_load_monitor.cpp
    src/monitor/cpu_stat_monitor.cpp
    src/monitor/disk_monitor.cpp
    src/monitor/mem_monitor.cpp
    src/monitor/net_monitor.cpp
    src/monitor/host_info_monitor.cpp
    src/monitor/metric_collector.cpp
    # observe
    src/observe/observe_collector.cpp
    # diagnose
    src/diagnose/bpf_diagnoser.cpp
    src/diagnose/perf_diagnoser.cpp
    src/diagnose/cmd_runner.cpp
    # config
    src/config/config_manager.cpp
    # rpc
    src/rpc/monitor_pusher.cpp
    # utils
    src/utils/read_file.cpp
)

add_executable(worker ${WORKER_SOURCES})

target_include_directories(worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ebpf
    ${CMAKE_BINARY_DIR}/proto
)

target_link_libraries(worker PRIVATE
    monitor_proto
    nlohmann_json::nlohmann_json
    pthread
)

if(ENABLE_EBPF)
    target_compile_definitions(worker PRIVATE ENABLE_EBPF)

    # Generate BPF skeleton headers from .bpf.o via bpftool
    set(BPF_SKEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/ebpf)
    set(PROC_SKEL  ${BPF_SKEL_DIR}/process_trace.skel.h)
    set(FLOW_SKEL  ${BPF_SKEL_DIR}/tcp_flow.skel.h)

    # Custom target to generate skeletons (requires bpftool and pre-compiled .bpf.o)
    add_custom_command(
        OUTPUT ${PROC_SKEL} ${FLOW_SKEL}
        COMMAND bpftool gen skeleton ${BPF_SKEL_DIR}/process_trace.bpf.o > ${PROC_SKEL}
        COMMAND bpftool gen skeleton ${BPF_SKEL_DIR}/tcp_flow.bpf.o      > ${FLOW_SKEL}
        DEPENDS ${BPF_SKEL_DIR}/process_trace.bpf.o ${BPF_SKEL_DIR}/tcp_flow.bpf.o
        COMMENT "Generating BPF skeleton headers"
    )
    add_custom_target(bpf_skeletons DEPENDS ${PROC_SKEL} ${FLOW_SKEL})
    add_dependencies(worker bpf_skeletons)

    # Link libbpf and libelf
    find_library(LIBBPF bpf REQUIRED)
    find_library(LIBELF elf REQUIRED)
    find_library(LIBZ   z   REQUIRED)
    target_link_libraries(worker PRIVATE ${LIBBPF} ${LIBELF} ${LIBZ})
endif()

if(ENABLE_PERF)
    target_compile_definitions(worker PRIVATE ENABLE_PERF)
endif()
