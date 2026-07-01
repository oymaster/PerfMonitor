#include "utils/read_file.h"
#include <fstream>
#include <sstream>

namespace monitor {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    if (!f.is_open()) return lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

} // namespace monitor
