#include "config/config_manager.h"
#include <fstream>
#include <iostream>

namespace monitor {

bool ConfigManager::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ConfigManager] cannot open " << path << std::endl;
        return false;
    }
    try {
        raw = nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        std::cerr << "[ConfigManager] JSON parse error: " << e.what() << std::endl;
        return false;
    }

    // Parse offcpu entries
    offcpu_entries.clear();
    if (raw.contains("offcpu") && raw["offcpu"].is_array()) {
        for (auto& item : raw["offcpu"]) {
            OffcpuEntry e;
            if (item.contains("process_name"))  e.process_name  = item["process_name"];
            if (item.contains("thread_name"))   e.thread_name   = item["thread_name"];
            if (item.contains("min_block_us"))  e.min_block_us  = item["min_block_us"];
            if (item.contains("max_block_us"))  e.max_block_us  = item["max_block_us"];
            if (item.contains("interval_s"))    e.interval_s    = item["interval_s"];
            if (item.contains("limit_file_size_mb")) e.limit_file_size_mb = item["limit_file_size_mb"];
            if (item.contains("limit_file_num"))     e.limit_file_num     = item["limit_file_num"];
            offcpu_entries.push_back(e);
        }
    }

    // Parse profile entries
    profile_entries.clear();
    if (raw.contains("profile") && raw["profile"].is_array()) {
        for (auto& item : raw["profile"]) {
            ProfileEntry e;
            if (item.contains("process_name"))     e.process_name     = item["process_name"];
            if (item.contains("thread_name"))      e.thread_name      = item["thread_name"];
            if (item.contains("max_nr"))           e.max_nr           = item["max_nr"];
            if (item.contains("max_size_kb"))      e.max_size_kb      = item["max_size_kb"];
            if (item.contains("cpus"))             e.cpus             = item["cpus"].get<std::string>();
            if (item.contains("cpu_threshold_pct")) e.cpu_threshold_pct = item["cpu_threshold_pct"];
            profile_entries.push_back(e);
        }
    }

    // Parse perf settings
    if (raw.contains("perf")) {
        auto& p = raw["perf"];
        if (p.contains("cpu_threshold_pct"))   perf_cpu_threshold_pct   = p["cpu_threshold_pct"];
        if (p.contains("overflow_duration_s")) perf_overflow_duration_s = p["overflow_duration_s"];
        if (p.contains("target_processes") && p["target_processes"].is_array()) {
            for (auto& tp : p["target_processes"]) perf_target_processes.push_back(tp);
        }
        if (p.contains("target_threads") && p["target_threads"].is_array()) {
            for (auto& tt : p["target_threads"]) perf_target_threads.push_back(tt);
        }
    }

    // Paths
    if (raw.contains("paths")) {
        auto& pt = raw["paths"];
        if (pt.contains("offcpu_bin"))   offcpu_bin   = pt["offcpu_bin"];
        if (pt.contains("profile_bin"))  profile_bin  = pt["profile_bin"];
        if (pt.contains("perf_bin"))     perf_bin     = pt["perf_bin"];
        if (pt.contains("output_dir"))   output_dir   = pt["output_dir"];
    }

    return true;
}

bool ConfigManager::Reload(const std::string& path) {
    return Load(path);
}

} // namespace monitor
