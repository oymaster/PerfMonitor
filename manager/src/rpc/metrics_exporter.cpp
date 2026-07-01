#include "rpc/metrics_exporter.h"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <sys/stat.h>

// Minimal embedded HTTP server (no external dependency).
// Serves Prometheus text format on /metrics and health on /healthz.

namespace monitor {

MetricsExporter::MetricsExporter(HostManager& host_mgr, int port)
    : host_mgr_(host_mgr), port_(port) {}

MetricsExporter::~MetricsExporter() { Stop(); }

void MetricsExporter::Start() {
    running_ = true;
    thread_ = std::thread(&MetricsExporter::serve_loop, this);
}

void MetricsExporter::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

// Parse value of a single query param (e.g. "file" from "?host=X&file=Y")
static std::string parse_param(const std::string& query, const std::string& key) {
    std::string prefix = key + "=";
    auto pos = query.find(prefix);
    if (pos == std::string::npos) return {};
    pos += prefix.size();
    auto end = query.find('&', pos);
    return (end == std::string::npos) ? query.substr(pos) : query.substr(pos, end - pos);
}

// Returns true if filename is safe (no path traversal, known prefix, ends in .data)
static bool is_safe_filename(const std::string& name) {
    if (name.find('/') != std::string::npos) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.size() < 6 || name.substr(name.size() - 5) != ".data") return false;
    return name.rfind("perf_", 0) == 0 ||
           name.rfind("offcpu_", 0) == 0 ||
           name.rfind("profile_", 0) == 0;
}

// Run a shell pipeline, return its stdout (empty on error).
static std::string run_pipeline(const std::string& cmd) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

// GET /api/events → JSON array of diagnose events across all hosts
static std::string api_events(HostManager& mgr) {
    auto hosts = mgr.GetAll();
    std::ostringstream ss;
    ss << "[\n";
    bool first_ev = true;
    for (const auto& h : hosts) {
        for (const auto& de : h.diagnose_events) {
            if (!first_ev) ss << ",\n";
            first_ev = false;
            // extract basename from file_path
            auto slash = de.file_path.rfind('/');
            std::string basename = (slash == std::string::npos)
                                   ? de.file_path
                                   : de.file_path.substr(slash + 1);
            ss << "  {\"host\":\"" << h.hostname << "\""
               << ",\"type\":\"" << de.type << "\""
               << ",\"process\":\"" << de.process_name << "\""
               << ",\"file\":\"" << basename << "\""
               << ",\"timestamp\":" << de.timestamp
               << ",\"cpu_percent\":" << de.cpu_percent
               << "}";
        }
    }
    ss << "\n]";
    return ss.str();
}

// GET /api/flamegraph?host=X&file=Y → SVG
static std::string api_flamegraph(const std::string& query) {
    std::string filename = parse_param(query, "file");
    if (!is_safe_filename(filename))
        return ""; // will send 400

    std::string path = "/tmp/monitor_system/diagnose/" + filename;

    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0)
        return "";

    std::string cmd;
    if (filename.rfind("perf_", 0) == 0) {
        // perf binary data → perf script → collapse → flamegraph
        cmd = "perf script -i " + path +
              " | /opt/FlameGraph/stackcollapse-perf.pl"
              " | /opt/FlameGraph/flamegraph.pl 2>/dev/null";
    } else {
        // offcpu/profile already produce collapsed stacks
        cmd = "/opt/FlameGraph/flamegraph.pl < " + path + " 2>/dev/null";
    }
    return run_pipeline(cmd);
}

static std::string prometheus_metrics(HostManager& mgr) {
    std::ostringstream ss;
    auto hosts = mgr.GetAll();

    // Host count
    ss << "# HELP monitor_host_count Number of active hosts\n";
    ss << "# TYPE monitor_host_count gauge\n";
    ss << "monitor_host_count " << hosts.size() << "\n";

    for (const auto& h : hosts) {
        std::string label = "host=\"" + h.hostname + "\"";

        // Health score
        ss << "# HELP monitor_host_health_score Host health score 0-100\n";
        ss << "# TYPE monitor_host_health_score gauge\n";
        ss << "monitor_host_health_score{" << label << "} " << h.health_score << "\n";

        // CPU
        ss << "# HELP monitor_cpu_load_1m CPU load 1-minute average\n";
        ss << "# TYPE monitor_cpu_load_1m gauge\n";
        ss << "monitor_cpu_load_1m{" << label << "} " << h.load_1m << "\n";

        ss << "# HELP monitor_cpu_used_pct CPU usage percentage\n";
        ss << "# TYPE monitor_cpu_used_pct gauge\n";
        ss << "monitor_cpu_used_pct{" << label << "} " << h.cpu_used_pct << "\n";

        // Memory
        ss << "# HELP monitor_mem_used_pct Memory usage percentage\n";
        ss << "# TYPE monitor_mem_used_pct gauge\n";
        ss << "monitor_mem_used_pct{" << label << "} " << h.mem_used_pct << "\n";

        // Disk
        ss << "# HELP monitor_disk_iops Disk IOPS\n";
        ss << "# TYPE monitor_disk_iops gauge\n";
        ss << "monitor_disk_iops{" << label << "} " << h.disk_iops << "\n";

        // Network
        ss << "# HELP monitor_net_rx_mbps Network RX Mbps\n";
        ss << "# TYPE monitor_net_rx_mbps gauge\n";
        ss << "monitor_net_rx_mbps{" << label << "} " << h.net_rx_mbps << "\n";

        ss << "# HELP monitor_net_tx_mbps Network TX Mbps\n";
        ss << "# TYPE monitor_net_tx_mbps gauge\n";
        ss << "monitor_net_tx_mbps{" << label << "} " << h.net_tx_mbps << "\n";

        // --- Observe: process lifecycle counters ---
        ss << "# HELP monitor_observe_proc_exec Process exec events (cumulative)\n";
        ss << "# TYPE monitor_observe_proc_exec gauge\n";
        ss << "monitor_observe_proc_exec{" << label << "} " << h.proc_exec_count << "\n";

        ss << "# HELP monitor_observe_proc_exit Process exit events (cumulative)\n";
        ss << "# TYPE monitor_observe_proc_exit gauge\n";
        ss << "monitor_observe_proc_exit{" << label << "} " << h.proc_exit_count << "\n";

        // --- Observe: TCP flow counters ---
        ss << "# HELP monitor_observe_tcp_flows Active TCP flows tracked\n";
        ss << "# TYPE monitor_observe_tcp_flows gauge\n";
        ss << "monitor_observe_tcp_flows{" << label << "} " << h.tcp_flow_count << "\n";

        ss << "# HELP monitor_observe_tcp_bytes_sent TCP bytes sent (cumulative)\n";
        ss << "# TYPE monitor_observe_tcp_bytes_sent gauge\n";
        ss << "monitor_observe_tcp_bytes_sent{" << label << "} " << h.tcp_bytes_sent << "\n";

        ss << "# HELP monitor_observe_tcp_bytes_recv TCP bytes received (cumulative)\n";
        ss << "# TYPE monitor_observe_tcp_bytes_recv gauge\n";
        ss << "monitor_observe_tcp_bytes_recv{" << label << "} " << h.tcp_bytes_recv << "\n";

        // --- Diagnose events ---
        ss << "# HELP monitor_diagnose_event_count Total diagnose records kept\n";
        ss << "# TYPE monitor_diagnose_event_count gauge\n";
        ss << "monitor_diagnose_event_count{" << label << "} "
           << h.diagnose_events.size() << "\n";

        for (const auto& de : h.diagnose_events) {
            std::string dlabel = "host=\"" + h.hostname + "\""
                                 ",type=\""    + de.type         + "\""
                                 ",process=\"" + de.process_name + "\""
                                 ",file=\""    + de.file_path    + "\"";
            ss << "# HELP monitor_diagnose_event Diagnose event CPU%% at trigger\n";
            ss << "# TYPE monitor_diagnose_event gauge\n";
            ss << "monitor_diagnose_event{" << dlabel << "} " << de.cpu_percent << "\n";
        }
    }
    return ss.str();
}

void MetricsExporter::serve_loop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[MetricsExporter] socket() failed\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[MetricsExporter] bind() failed on port " << port_ << "\n";
        close(server_fd);
        return;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "[MetricsExporter] listen() failed\n";
        close(server_fd);
        return;
    }

    std::cout << "[MetricsExporter] HTTP /metrics on :" << port_ << "\n";

    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd, &fds);

        struct timeval tv = {1, 0};
        int ret = select(server_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        // Read request (ignore body)
        char buf[4096] = {};
        read(client_fd, buf, sizeof(buf) - 1);

        std::string req(buf);
        // Extract request line: "GET /path?query HTTP/1.1"
        auto line_end = req.find("\r\n");
        std::string req_line = (line_end != std::string::npos) ? req.substr(0, line_end) : req;
        // path = everything between first space and second space (or '?' for query)
        auto p1 = req_line.find(' ');
        auto p2 = req_line.rfind(' ');
        std::string full_path = (p1 != std::string::npos && p2 > p1)
                                ? req_line.substr(p1 + 1, p2 - p1 - 1) : "/";
        auto q_pos  = full_path.find('?');
        std::string path  = (q_pos != std::string::npos) ? full_path.substr(0, q_pos) : full_path;
        std::string query = (q_pos != std::string::npos) ? full_path.substr(q_pos + 1) : "";

        std::string response;
        if (path == "/healthz") {
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
        } else if (path == "/api/events") {
            std::string body = api_events(host_mgr_);
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        } else if (path == "/api/flamegraph") {
            std::string body = api_flamegraph(query);
            if (body.empty()) {
                response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\n"
                           "Invalid or missing file parameter";
            } else {
                response = "HTTP/1.1 200 OK\r\nContent-Type: image/svg+xml\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
            }
        } else {
            std::string body = prometheus_metrics(host_mgr_);
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\n"
                       "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        }

        write(client_fd, response.c_str(), response.size());
        close(client_fd);
    }

    close(server_fd);
}

} // namespace monitor
