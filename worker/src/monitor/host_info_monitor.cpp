#include "monitor/host_info_monitor.h"
#include "utils/read_file.h"
#include <unistd.h>
#include <cstring>
#include <sys/utsname.h>
#include <netdb.h>
#include <arpa/inet.h>

namespace monitor {

bool HostInfoMonitor::Init() {
    // hostname
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf)) == 0) {
        hostname = buf;
    }

    // primary IP (simple: resolve hostname)
    struct addrinfo hints = {}, *info = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(buf, nullptr, &hints, &info) == 0 && info) {
        char ip_str[INET_ADDRSTRLEN] = {};
        auto* addr = reinterpret_cast<sockaddr_in*>(info->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        ip = ip_str;
        freeaddrinfo(info);
    }
    if (ip.empty()) ip = "127.0.0.1";

    return true;
}

} // namespace monitor
