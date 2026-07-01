#pragma once
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/types.h>

namespace monitor {

/// Fork + exec wrapper for running external diagnostic tools.
/// Non-blocking: Start() forks a child, Stop() sends SIGTERM.
struct CmdRunner {
    ~CmdRunner() { Stop(); }

    bool Start(const std::string& bin, const std::vector<std::string>& args,
               const std::string& output_file = "");
    void Stop();
    bool IsRunning();

    pid_t pid() const { return pid_; }
    const std::string& output_path() const { return output_path_; }

private:
    pid_t pid_ = -1;
    std::string output_path_;
};

} // namespace monitor
