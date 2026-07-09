#include "rpc/grpc_server.h"
#include "rpc/metrics_exporter.h"
#include "host_manager.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>
#include <cstdlib>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static const char* env_or_default(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

int main(int argc, char* argv[]) {
    std::string grpc_addr = env_or_default("PERFMONITOR_GRPC_ADDR", "0.0.0.0:50051");
    int         http_port = std::stoi(env_or_default("PERFMONITOR_HTTP_PORT", "8080"));

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--grpc-addr" && i + 1 < argc) {
            grpc_addr = argv[++i];
        } else if (arg == "--http-port" && i + 1 < argc) {
            http_port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--grpc-addr ADDR] [--http-port PORT]\n";
            return 0;
        }
    }

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
