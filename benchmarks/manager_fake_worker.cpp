#include "monitor_info.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string addr = "127.0.0.1:50051";
    int workers = 100;
    int iterations = 10;
    int concurrency = 8;
    int interval_ms = 0;
    int timeout_ms = 1000;
};

struct Metrics {
    std::mutex mutex;
    std::vector<double> latencies_ms;
    std::atomic<int> ok{0};
    std::atomic<int> failed{0};
};

void usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
              << "  --addr HOST:PORT       Manager gRPC address (default: 127.0.0.1:50051)\n"
              << "  --workers N            logical fake workers (default: 100)\n"
              << "  --iterations N         reports per worker (default: 10)\n"
              << "  --concurrency N        client threads (default: 8)\n"
              << "  --interval-ms N        sleep between iterations per worker batch (default: 0)\n"
              << "  --timeout-ms N         per RPC deadline (default: 1000)\n";
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };
        if (arg == "--addr") options.addr = require_value("--addr");
        else if (arg == "--workers") options.workers = std::stoi(require_value("--workers"));
        else if (arg == "--iterations") options.iterations = std::stoi(require_value("--iterations"));
        else if (arg == "--concurrency") options.concurrency = std::stoi(require_value("--concurrency"));
        else if (arg == "--interval-ms") options.interval_ms = std::stoi(require_value("--interval-ms"));
        else if (arg == "--timeout-ms") options.timeout_ms = std::stoi(require_value("--timeout-ms"));
        else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    options.workers = std::max(1, options.workers);
    options.iterations = std::max(1, options.iterations);
    options.concurrency = std::max(1, options.concurrency);
    return options;
}

double percentile(std::vector<double> values, double pct) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>((values.size() - 1) * pct);
    return values[index];
}

monitor::MonitorInfo make_report(int worker_index, int iteration) {
    monitor::MonitorInfo info;
    info.set_hostname("fake-worker-" + std::to_string(worker_index));
    info.set_ip("10.0.0." + std::to_string(worker_index % 250 + 1));
    info.set_timestamp(std::time(nullptr));

    auto* load = info.mutable_cpu_load();
    load->set_load_1m(0.2 + (worker_index % 8) * 0.1);
    load->set_load_5m(0.3);
    load->set_load_15m(0.4);
    load->set_nr_cores(8);

    auto* stat = info.mutable_cpu_stat();
    stat->set_user_pct(10 + (iteration % 20));
    stat->set_system_pct(5);
    stat->set_idle_pct(85 - (iteration % 20));
    stat->set_iowait_pct(1);
    stat->set_steal_pct(0);
    stat->set_softirq_pct(1);

    auto* mem = info.mutable_mem();
    mem->set_total_gb(16);
    mem->set_used_gb(4 + (worker_index % 4));
    mem->set_free_gb(8);
    mem->set_available_gb(10);
    mem->set_cached_gb(2);
    mem->set_buffers_gb(1);
    mem->set_swap_total_gb(0);
    mem->set_swap_used_gb(0);
    mem->set_used_pct(30 + (worker_index % 20));

    auto* disk = info.add_disks();
    disk->set_device("vda");
    disk->set_read_mbps(5 + (iteration % 5));
    disk->set_write_mbps(3 + (iteration % 3));
    disk->set_iops(100 + worker_index % 50);
    disk->set_io_latency_ms(2 + iteration % 4);

    auto* net = info.add_nets();
    net->set_iface("eth0");
    net->set_rx_mbps(10 + worker_index % 10);
    net->set_tx_mbps(6 + iteration % 10);
    net->set_rx_packets(1000 + iteration);
    net->set_tx_packets(900 + iteration);
    net->set_rx_dropped(worker_index % 3 == 0 ? 1 : 0);
    net->set_tx_dropped(0);
    net->set_rx_errors(0);
    net->set_tx_errors(0);

    auto* observe = info.mutable_observe_batch();
    auto* process = observe->add_process_events();
    process->set_comm("fake-process");
    process->set_pid(1000 + worker_index);
    process->set_ppid(1);
    process->set_filename("/usr/bin/fake-process");
    process->set_event(iteration % 2 == 0 ? "exec" : "exit");
    process->set_ts_ns(static_cast<std::uint64_t>(std::time(nullptr)) * 1000 * 1000 * 1000);

    auto* flow = observe->add_flow_events();
    flow->set_pid(1000 + worker_index);
    flow->set_comm("fake-process");
    flow->set_src_ip("10.0.0.1");
    flow->set_src_port(30000 + worker_index % 1000);
    flow->set_dst_ip("10.0.0.2");
    flow->set_dst_port(8080);
    flow->set_bytes_sent(1024 + iteration);
    flow->set_bytes_recv(2048 + iteration);
    flow->set_connect_latency_ns(1000000);
    flow->set_state("ESTABLISHED");

    return info;
}

void run_thread(int thread_index, const Options& options, Metrics& metrics) {
    auto channel = grpc::CreateChannel(options.addr, grpc::InsecureChannelCredentials());
    auto stub = monitor::MonitorService::NewStub(channel);
    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        for (int worker = thread_index; worker < options.workers; worker += options.concurrency) {
            auto request = make_report(worker, iteration);
            monitor::SetResponse response;
            grpc::ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options.timeout_ms));

            const auto started = std::chrono::steady_clock::now();
            const auto status = stub->SetMonitorInfo(&context, request, &response);
            const auto elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            {
                std::lock_guard<std::mutex> lock(metrics.mutex);
                metrics.latencies_ms.push_back(elapsed_ms);
            }
            if (status.ok() && response.ok()) ++metrics.ok;
            else ++metrics.failed;
        }
        if (options.interval_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_args(argc, argv);
        Metrics metrics;
        const auto started = std::chrono::steady_clock::now();
        std::vector<std::thread> threads;
        const int thread_count = std::min(options.concurrency, options.workers);
        threads.reserve(thread_count);
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back(run_thread, i, std::cref(options), std::ref(metrics));
        }
        for (auto& thread : threads) thread.join();
        const auto elapsed_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();

        std::vector<double> latencies;
        {
            std::lock_guard<std::mutex> lock(metrics.mutex);
            latencies = metrics.latencies_ms;
        }
        const double avg = latencies.empty()
            ? 0.0
            : std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        const int total = options.workers * options.iterations;
        std::cout << std::fixed << std::setprecision(3)
                  << "{\n"
                  << "  \"addr\": \"" << options.addr << "\",\n"
                  << "  \"workers\": " << options.workers << ",\n"
                  << "  \"iterations\": " << options.iterations << ",\n"
                  << "  \"concurrency\": " << thread_count << ",\n"
                  << "  \"total_reports\": " << total << ",\n"
                  << "  \"ok\": " << metrics.ok.load() << ",\n"
                  << "  \"failed\": " << metrics.failed.load() << ",\n"
                  << "  \"elapsed_s\": " << elapsed_s << ",\n"
                  << "  \"reports_per_s\": " << (elapsed_s > 0 ? total / elapsed_s : 0.0) << ",\n"
                  << "  \"avg_ms\": " << avg << ",\n"
                  << "  \"p50_ms\": " << percentile(latencies, 0.50) << ",\n"
                  << "  \"p95_ms\": " << percentile(latencies, 0.95) << ",\n"
                  << "  \"p99_ms\": " << percentile(latencies, 0.99) << ",\n"
                  << "  \"max_ms\": " << (latencies.empty() ? 0.0 : *std::max_element(latencies.begin(), latencies.end())) << "\n"
                  << "}\n";
        return metrics.failed.load() == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "manager_fake_worker failed: " << error.what() << '\n';
        return 2;
    }
}
