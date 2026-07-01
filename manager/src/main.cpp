#include "rpc/grpc_server.h"
#include "rpc/metrics_exporter.h"
#include "host_manager.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string grpc_addr = "0.0.0.0:50051";
    int         http_port = 8080;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "[Manager] Starting monitor_system Manager v2.0\n";

    monitor::HostManager host_mgr;

    // Start gRPC server
    monitor::GrpcServer grpc_srv(host_mgr);
    grpc_srv.Start(grpc_addr);

    // Start Prometheus exporter
    monitor::MetricsExporter exporter(host_mgr, http_port);
    exporter.Start();

    std::cout << "[Manager] Ready. gRPC=" << grpc_addr
              << " | HTTP=:" << http_port << "\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        host_mgr.RemoveExpired(60);
    }

    std::cout << "[Manager] Shutting down...\n";
    exporter.Stop();
    std::cout << "[Manager] Done.\n";
    return 0;
}
