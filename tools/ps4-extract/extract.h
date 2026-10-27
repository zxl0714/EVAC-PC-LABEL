#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ps4extract {
namespace fs = std::filesystem;
struct Entry { unsigned index; std::wstring name; };
struct Error { std::wstring message; };
struct Options {
  fs::path input, output, tool;
  std::wstring passcode = L"00000000000000000000000000000000";
};
using Progress = std::function<void(const std::wstring&)>;
std::vector<Entry> read_entries(const fs::path& input);
std::wstring quote(const std::wstring& argument);
fs::path extract(const Options& options, const Progress& progress, const std::atomic_bool& cancel);
}
