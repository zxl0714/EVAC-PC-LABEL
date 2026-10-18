#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cave {
namespace fs = std::filesystem;
struct DataFile {
  std::string_view path;
  uint64_t bytes;
  std::string_view sha256;
  bool program;
};
struct Game {
  std::string_view id, name, backend, data_dir, executable;
  bool integrated, coming_soon;
  std::span<const DataFile> files;
  std::span<const std::string_view> runtime_files;
};
std::span<const Game> Catalogue();
const Game& FindGame(std::string_view id);
std::string Sha256(const fs::path& file);
std::string Utf8(const fs::path& path);
fs::path FromUtf8(std::string_view text);

enum class CheckLevel { Quick, Programs, Full };
enum class DataState { Missing, Incomplete, Unsupported, Unverified, Verified, Unknown, Invalid };
std::string_view Name(DataState state);
struct Inspection {
  std::string game, state, reason;
  DataState data_state{DataState::Unknown};
  fs::path data_dir;
  uint32_t files_checked{}, hashes_checked{};
  std::vector<std::string> problems, missing_runtime;
};
Inspection Inspect(const Game& game, const fs::path& collection_root,
                   const fs::path& data_dir, CheckLevel level);
}  // namespace cave
