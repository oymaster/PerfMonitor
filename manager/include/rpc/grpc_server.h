#pragma once
#include "monitor_info.grpc.pb.h"
#include "host_manager.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace monitor {

/// gRPC server implementing MonitorService.
class GrpcServer final : public MonitorService::Service {
public:
    explicit GrpcServer(HostManager& host_mgr);

    grpc::Status SetMonitorInfo(
        grpc::ServerContext* ctx,
        const MonitorInfo* request,
        SetResponse* response) override;

    grpc::Status GetMonitorInfo(
        grpc::ServerContext* ctx,
        const GetRequest* request,
        MonitorInfo* response) override;

    void Start(const std::string& addr);

private:
    HostManager& host_mgr_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace monitor
