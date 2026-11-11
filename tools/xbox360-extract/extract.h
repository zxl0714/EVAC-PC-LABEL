#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace xboxextract {
namespace fs = std::filesystem;
struct Options { fs::path input, output; };
struct Progress { uint64_t done{}, total{}; std::wstring file; };
using ProgressCallback = std::function<void(const Progress&)>;
// Publishes a new directory only after every file has been copied. Existing
// destinations are never modified. Cancellation removes this run's staging tree.
fs::path Extract(const Options&, const ProgressCallback&, const std::atomic_bool& cancel);
}
