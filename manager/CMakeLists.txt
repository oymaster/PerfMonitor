set(MANAGER_SOURCES
    src/main.cpp
    src/host_manager.cpp
    src/rpc/grpc_server.cpp
    src/rpc/metrics_exporter.cpp
)

add_executable(manager ${MANAGER_SOURCES})

target_include_directories(manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/proto
)

target_link_libraries(manager PRIVATE
    monitor_proto
    pthread
)
