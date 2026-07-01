#include "rpc/monitor_pusher.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <getopt.h>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "  -a, --addr ADDR      Manager gRPC address (default: localhost:50051)\n"
              << "  -i, --interval SEC   Push interval in seconds (default: 10)\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string manager_addr = "localhost:50051";
    int         interval_s   = 10;

    static struct option long_opts[] = {
        {"addr",     required_argument, nullptr, 'a'},
        {"interval", required_argument, nullptr, 'i'},
        {"help",     no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "a:i:h", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'a': manager_addr = optarg; break;
        case 'i': interval_s = std::stoi(optarg); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "[Worker] Starting monitor_system Worker v2.0\n"
              << "[Worker] Manager: " << manager_addr
              << " | Interval: " << interval_s << "s\n";

    monitor::MonitorPusher pusher(manager_addr, interval_s);
    pusher.Run();

    std::cout << "[Worker] Running. Press Ctrl+C to stop.\n";
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Worker] Shutting down...\n";
    pusher.Stop();
    std::cout << "[Worker] Done.\n";
    return 0;
}
