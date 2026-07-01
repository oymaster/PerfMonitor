#include "diagnose/cmd_runner.h"
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>

namespace monitor {

bool CmdRunner::Start(const std::string& bin, const std::vector<std::string>& args,
                       const std::string& output_file) {
    output_path_ = output_file;

    pid_ = fork();
    if (pid_ < 0) {
        std::cerr << "[CmdRunner] fork failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (pid_ == 0) {
        // child: die when parent dies, regardless of how parent exits
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        if (!output_file.empty()) {
            int fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }

        // Close all inherited fds > 2 so BPF/socket fds from the parent
        // don't conflict with BCC tools' own BPF program loading.
        {
            DIR* d = opendir("/proc/self/fd");
            if (d) {
                int dfd = dirfd(d);
                struct dirent* ent;
                while ((ent = readdir(d)) != nullptr) {
                    if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
                    int fdno = atoi(ent->d_name);
                    if (fdno > 2 && fdno != dfd) close(fdno);
                }
                closedir(d);
            }
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(bin.c_str()));
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        execv(bin.c_str(), argv.data());
        _exit(127); // exec failed
    }

    return true;
}

void CmdRunner::Stop() {
    if (pid_ <= 0) return;
    kill(pid_, SIGTERM);
    // non-blocking: collect zombie in destructor
    int status;
    waitpid(pid_, &status, WNOHANG);
    pid_ = -1;
}

bool CmdRunner::IsRunning() {
    if (pid_ <= 0) return false;
    int status;
    pid_t ret = waitpid(pid_, &status, WNOHANG);
    if (ret == pid_) { pid_ = -1; return false; }  // exited — reap it
    if (ret < 0)     { pid_ = -1; return false; }  // no such process
    return true;  // ret == 0: still running
}

} // namespace monitor
