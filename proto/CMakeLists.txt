find_package(Protobuf REQUIRED)

# Find gRPC manually (apt-installed packages have no cmake config)
find_library(GRPC_LIB    grpc    REQUIRED)
find_library(GRPCPP_LIB  grpc++  REQUIRED)
find_library(GRPCPP_REFL grpc++_reflection REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PROTO_BIN ${CMAKE_CURRENT_BINARY_DIR})

set(PROTO_FILES
    cpu_load.proto
    cpu_stat.proto
    disk_info.proto
    mem_info.proto
    net_info.proto
    monitor_info.proto
)

# Generate .pb.cc / .pb.h for every proto file
set(ALL_PROTO_SRCS)
set(ALL_PROTO_HDRS)
foreach(proto ${PROTO_FILES})
    get_filename_component(base ${proto} NAME_WE)
    set(src "${PROTO_BIN}/${base}.pb.cc")
    set(hdr "${PROTO_BIN}/${base}.pb.h")
    list(APPEND ALL_PROTO_SRCS ${src})
    list(APPEND ALL_PROTO_HDRS ${hdr})
    add_custom_command(
        OUTPUT ${src} ${hdr}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${PROTO_BIN}
            -I${PROTO_DIR}
            ${PROTO_DIR}/${proto}
        DEPENDS ${PROTO_DIR}/${proto}
        COMMENT "protoc: ${proto}"
    )
endforeach()

# Generate gRPC stubs only for monitor_info (the only file with a service)
set(GRPC_SRC "${PROTO_BIN}/monitor_info.grpc.pb.cc")
set(GRPC_HDR "${PROTO_BIN}/monitor_info.grpc.pb.h")
add_custom_command(
    OUTPUT ${GRPC_SRC} ${GRPC_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --grpc_out=${PROTO_BIN}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${PROTO_DIR}
        ${PROTO_DIR}/monitor_info.proto
    DEPENDS ${PROTO_DIR}/monitor_info.proto
    COMMENT "grpc_cpp_plugin: monitor_info.proto"
)

add_library(monitor_proto
    ${ALL_PROTO_SRCS}
    ${GRPC_SRC}
)

target_include_directories(monitor_proto PUBLIC
    ${PROTO_BIN}
    ${PROTO_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(monitor_proto PUBLIC
    ${Protobuf_LIBRARIES}
    ${GRPCPP_LIB}
    ${GRPC_LIB}
    ${GRPCPP_REFL}
    pthread
)
