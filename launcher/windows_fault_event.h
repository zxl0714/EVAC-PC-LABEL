#pragma once
#include <cstdint>
#include <string>

namespace cave::launcher::reports {
std::string WindowsApplicationFault(unsigned long pid, std::uint64_t created);
}
