#include "rpc/grpc_server.h"
#include <iostream>

namespace monitor {

GrpcServer::GrpcServer(HostManager& host_mgr) : host_mgr_(host_mgr) {}

grpc::Status GrpcServer::SetMonitorInfo(
    grpc::ServerContext* ctx,
    const MonitorInfo* request,
    SetResponse* response) {
    HostSnapshot snap;
    snap.last_update  = time(nullptr);
    snap.load_1m      = request->cpu_load().load_1m();
    snap.cpu_used_pct = 100.0 - request->cpu_stat().idle_pct();
    snap.mem_used_pct = request->mem().used_pct();

    // Aggregate disk iops
    double total_iops = 0;
    for (auto& d : request->disks()) total_iops += d.iops();
    snap.disk_iops = total_iops;

    // Aggregate net throughput
    double rx = 0, tx = 0, rx_dropped = 0;
    for (auto& n : request->nets()) {
        rx += n.rx_mbps();
        tx += n.tx_mbps();
        rx_dropped += n.rx_dropped();
    }
    snap.net_rx_mbps = rx;
    snap.net_tx_mbps = tx;
    snap.net_rx_dropped = rx_dropped;

    // --- Consume observe events ---
    if (request->has_observe_batch()) {
        const auto& batch = request->observe_batch();
        for (const auto& pe : batch.process_events()) {
            if (pe.event() == "exec") {
                snap.proc_exec_count++;
            } else {
                snap.proc_exit_count++;
            }
        }
        for (const auto& fe : batch.flow_events()) {
            snap.tcp_flow_count++;
            snap.tcp_bytes_sent += fe.bytes_sent();
            snap.tcp_bytes_recv += fe.bytes_recv();
        }
    }

    // --- Consume diagnose events ---
    for (const auto& de : request->diagnose_events()) {
        DiagnoseRecord rec;
        rec.type         = de.type();
        rec.process_name = de.process_name();
        rec.file_path    = de.file_path();
        rec.timestamp    = de.timestamp();
        rec.cpu_percent  = de.cpu_percent();
        snap.diagnose_events.push_back(rec);
    }

    host_mgr_.Update(request->hostname(), snap);

    response->set_ok(true);
    response->set_message("OK");
    return grpc::Status::OK;
}

grpc::Status GrpcServer::GetMonitorInfo(
    grpc::ServerContext* ctx,
    const GetRequest* request,
    MonitorInfo* response) {
    auto snap = host_mgr_.Get(request->hostname());
    response->set_hostname(request->hostname());
    response->set_timestamp(snap.last_update);

    auto* load = response->mutable_cpu_load();
    load->set_load_1m(snap.load_1m);

    auto* stat = response->mutable_cpu_stat();
    stat->set_idle_pct(100.0 - snap.cpu_used_pct);

    auto* mem = response->mutable_mem();
    mem->set_used_pct(snap.mem_used_pct);

    return grpc::Status::OK;
}

void GrpcServer::Start(const std::string& addr) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(this);

    server_ = builder.BuildAndStart();
    std::cout << "[Manager] gRPC server listening on " << addr << std::endl;
}

} // namespace monitor
