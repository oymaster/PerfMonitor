#include "rpc/monitor_pusher.h"
#include "config/config_manager.h"
#include "monitor_info.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <chrono>

namespace monitor {

// ============================================================
// Internal gRPC stub
// ============================================================
struct MonitorPusher::Impl {
    std::unique_ptr<MonitorService::Stub>   stub;
    std::shared_ptr<grpc::Channel>          channel;

    bool connect(const std::string& addr) {
        channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
        if (!channel->WaitForConnected(deadline)) {
            std::cerr << "[MonitorPusher] gRPC connect timeout: " << addr << std::endl;
            return false;
        }
        stub = MonitorService::NewStub(channel);
        return true;
    }
};

MonitorPusher::MonitorPusher(const std::string& manager_addr, int interval_s)
    : manager_addr_(manager_addr), interval_s_(interval_s),
      impl_(std::make_unique<Impl>()) {}

MonitorPusher::~MonitorPusher() { Stop(); }

void MonitorPusher::Run() {
    if (!impl_->connect(manager_addr_)) {
        std::cerr << "[MonitorPusher] Failed to connect to Manager, retrying..." << std::endl;
    }

    // Init first-layer monitors (no config dependency)
    metrics_.Init();
    observer_.Init();

    // Load diagnose config before diagnoser Init (Init needs output_dir)
    ConfigManager cfg;
    cfg.Load("conf/diagnose_conf.json");

    // Apply config to diagnosers
    bpf_diagnoser_.output_dir_    = cfg.output_dir;
    bpf_diagnoser_.offcpu_bin_    = cfg.offcpu_bin;
    bpf_diagnoser_.profile_bin_   = cfg.profile_bin;
    if (!cfg.offcpu_entries.empty()) {
        bpf_diagnoser_.limit_file_num_     = cfg.offcpu_entries[0].limit_file_num;
        bpf_diagnoser_.limit_file_size_mb_ = cfg.offcpu_entries[0].limit_file_size_mb;
    }
    perf_diagnoser_.output_dir_ = cfg.output_dir;
    perf_diagnoser_.perf_bin_   = cfg.perf_bin;
    perf_diagnoser_.cpu_threshold_pct_   = cfg.perf_cpu_threshold_pct;
    perf_diagnoser_.overflow_duration_s_ = cfg.perf_overflow_duration_s;
    perf_diagnoser_.target_processes_    = cfg.perf_target_processes;
    perf_diagnoser_.target_threads_      = cfg.perf_target_threads;
    if (!cfg.offcpu_entries.empty())
        perf_diagnoser_.limit_file_num_  = cfg.offcpu_entries[0].limit_file_num;

    // Init diagnosers after config is applied
    bpf_diagnoser_.Init();
    perf_diagnoser_.Init();

    // Start offcpu/profile for configured entries
    for (auto& e : cfg.offcpu_entries) {
        bpf_diagnoser_.start_offcpu(e.process_name, e.thread_name,
                                     e.min_block_us, e.max_block_us, e.interval_s);
    }
    for (auto& e : cfg.profile_entries) {
        bpf_diagnoser_.start_profile(e.process_name, e.thread_name,
                                      e.max_nr, e.max_size_kb, e.cpus);
    }

    running_ = true;
    loop_thread_ = std::thread(&MonitorPusher::push_loop, this);
}

void MonitorPusher::Stop() {
    running_ = false;
    if (loop_thread_.joinable()) loop_thread_.join();
    bpf_diagnoser_.Reset();
    perf_diagnoser_.Stop();  // full stop: kills monitoring thread
}

void MonitorPusher::push_loop() {
    while (running_) {
        do_push();
        std::this_thread::sleep_for(std::chrono::seconds(interval_s_));
    }
}

void MonitorPusher::do_push() {
    // Layer 1: metric collection
    metrics_.Collect();

    // Layer 2: eBPF observe
    observer_.Collect();

    // Layer 3 & 4: diagnose (events are async; Collect polls)
    bpf_diagnoser_.Collect();
    perf_diagnoser_.Collect();

    // Build protobuf
    MonitorInfo info;
    info.set_hostname(metrics_.host.hostname);
    info.set_ip(metrics_.host.ip);
    info.set_timestamp(time(nullptr));

    // --- Layer 1: metrics ---
    {
        auto* load = info.mutable_cpu_load();
        load->set_load_1m(metrics_.cpu_load.load_1m);
        load->set_load_5m(metrics_.cpu_load.load_5m);
        load->set_load_15m(metrics_.cpu_load.load_15m);
        load->set_nr_cores(metrics_.cpu_load.nr_cores);

        auto* stat = info.mutable_cpu_stat();
        stat->set_user_pct(metrics_.cpu_stat.pct.user);
        stat->set_system_pct(metrics_.cpu_stat.pct.system);
        stat->set_idle_pct(metrics_.cpu_stat.pct.idle);
        stat->set_iowait_pct(metrics_.cpu_stat.pct.iowait);
        stat->set_steal_pct(metrics_.cpu_stat.pct.steal);
        stat->set_softirq_pct(metrics_.cpu_stat.pct.softirq);

        auto* mem = info.mutable_mem();
        mem->set_total_gb(metrics_.mem.total_gb);
        mem->set_used_gb(metrics_.mem.used_gb);
        mem->set_free_gb(metrics_.mem.free_gb);
        mem->set_available_gb(metrics_.mem.available_gb);
        mem->set_cached_gb(metrics_.mem.cached_gb);
        mem->set_buffers_gb(metrics_.mem.buffers_gb);
        mem->set_swap_total_gb(metrics_.mem.swap_total_gb);
        mem->set_swap_used_gb(metrics_.mem.swap_used_gb);
        mem->set_used_pct(metrics_.mem.used_pct);

        for (auto& d : metrics_.disk.disks) {
            auto* dd = info.add_disks();
            dd->set_device(d.device);
            dd->set_read_mbps(d.read_mbps);
            dd->set_write_mbps(d.write_mbps);
            dd->set_iops(d.iops);
            dd->set_io_latency_ms(d.io_latency_ms);
        }

        for (auto& n : metrics_.net.interfaces) {
            auto* nn = info.add_nets();
            nn->set_iface(n.iface);
            nn->set_rx_mbps(n.rx_mbps);
            nn->set_tx_mbps(n.tx_mbps);
            nn->set_rx_packets(n.rx_packets);
            nn->set_tx_packets(n.tx_packets);
            nn->set_rx_dropped(n.rx_dropped);
            nn->set_tx_dropped(n.tx_dropped);
            nn->set_rx_errors(n.rx_errors);
            nn->set_tx_errors(n.tx_errors);
        }
    }

    // --- Layer 2: observe ---
    {
        auto* ob = info.mutable_observe_batch();
        for (auto& pe : observer_.process_events) {
            auto* p = ob->add_process_events();
            p->set_comm(pe.comm);
            p->set_pid(pe.pid);
            p->set_ppid(pe.ppid);
            p->set_filename(pe.filename);
            p->set_event(pe.event);
            p->set_ts_ns(pe.ts_ns);
        }
        for (auto& fe : observer_.flow_events) {
            auto* f = ob->add_flow_events();
            f->set_pid(fe.pid);
            f->set_comm(fe.comm);
            f->set_ppid_comm(fe.ppid_comm);
            f->set_exe(fe.exe);
            f->set_src_ip(fe.src_ip);
            f->set_src_port(fe.src_port);
            f->set_dst_ip(fe.dst_ip);
            f->set_dst_port(fe.dst_port);
            f->set_bytes_sent(fe.bytes_sent);
            f->set_bytes_recv(fe.bytes_recv);
            f->set_connect_latency_ns(fe.connect_latency_ns);
            f->set_state(fe.state);
            f->set_start_ns(fe.start_ns);
            f->set_last_ns(fe.last_ns);
        }
        observer_.Reset();
    }

    // --- Layer 3 & 4: diagnose ---
    {
        for (auto& ev : bpf_diagnoser_.events) {
            auto* de = info.add_diagnose_events();
            de->set_type(ev.type);
            de->set_process_name(ev.process_name);
            de->set_thread_name(ev.thread_name);
            de->set_file_path(ev.file_path);
            de->set_timestamp(ev.timestamp);
            de->set_cpu_percent(ev.cpu_percent);
        }
        bpf_diagnoser_.Reset();

        for (auto& ev : perf_diagnoser_.events) {
            auto* de = info.add_diagnose_events();
            de->set_type(ev.type);
            de->set_process_name(ev.process_name);
            de->set_thread_name(ev.thread_name);
            de->set_file_path(ev.file_path);
            de->set_timestamp(ev.timestamp);
            de->set_cpu_percent(ev.cpu_percent);
        }
        perf_diagnoser_.Reset();
    }

    // Push
    if (impl_->stub) {
        grpc::ClientContext ctx;
        SetResponse resp;
        auto status = impl_->stub->SetMonitorInfo(&ctx, info, &resp);
        if (!status.ok()) {
            std::cerr << "[MonitorPusher] gRPC push failed: " << status.error_message() << std::endl;
        }
    }
}

} // namespace monitor
