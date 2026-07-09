add_executable(manager_fake_worker
    manager_fake_worker.cpp
)

target_include_directories(manager_fake_worker PRIVATE
    ${CMAKE_BINARY_DIR}/proto
)

target_link_libraries(manager_fake_worker PRIVATE
    monitor_proto
    pthread
)
