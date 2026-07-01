#pragma once
#include <string>
#include <vector>

namespace monitor {

/// Read entire file into string.
std::string ReadFile(const std::string& path);

/// Read file, split by newline.
std::vector<std::string> ReadLines(const std::string& path);

} // namespace monitor
