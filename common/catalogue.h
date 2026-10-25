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
struct ContentPackage {
  std::string_view id, name, directory, package_name;
  uint32_t title_id;
  std::span<const DataFile> files;
  std::string_view native_name{};
};
struct DataVariant {
  std::string_view id, version, layout, entry;
  std::span<const DataFile> files;
  std::span<const std::string_view> forbidden_files;
};
struct Game {
  std::string_view id, name, backend, data_dir, executable;
  bool integrated, coming_soon;
  std::span<const DataFile> files;
  std::span<const std::string_view> runtime_files;
  std::span<const ContentPackage> content{};
  bool community_supported{true};
  std::span<const std::string_view> forbidden_files{};
  std::string_view name_japanese{};
  std::span<const DataVariant> variants{};
  std::string_view DisplayName() const { return name_japanese.empty() ? name : name_japanese; }
};
std::span<const Game> Catalogue();
const Game& FindGame(std::string_view id);
// Select only by the actual root program's size and SHA-256. Full inspection
// must still validate the remaining programs, resources and forbidden files.
const DataVariant* IdentifyDataVariant(const Game& game,const fs::path& data,uint32_t* hashes=nullptr);
std::span<const DataFile> DataFiles(const Game& game,std::string_view variant);
std::string Sha256(const fs::path& file);
std::string Utf8(const fs::path& path);
fs::path FromUtf8(std::string_view text);

enum class CheckLevel { Quick, Programs, Full };
enum class DataState { Missing, Incomplete, Unsupported, Unverified, Verified, Unknown, Invalid, Nested };
std::string_view Name(DataState state);
struct Inspection {
  std::string game, state, reason;
  DataState data_state{DataState::Unknown};
  fs::path data_dir;
  uint32_t files_checked{}, hashes_checked{};
  std::vector<std::string> problems, missing_runtime;
  // Structural hints only: none of these candidates has passed validation.
  std::vector<std::string> candidate_roots;
  std::string variant, version, layout;
};
Inspection Inspect(const Game& game, const fs::path& collection_root,
                   const fs::path& data_dir, CheckLevel level);
struct ContentInspection {
  std::string id, name;
  DataState state{DataState::Missing};
  uint32_t files_checked{}, hashes_checked{};
  std::vector<std::string> problems;
  fs::path directory;
  fs::path container; // Nonempty for a raw LIVE/PIRS input; directory is its parent.
};
// Manifest paths remain canonical for packaging; input folders need not be.
fs::path ContentRelativePath(const ContentPackage& package, const DataFile& file);
// Optional packages are independent of base-game readiness. Only a Full result
// with state Verified may be exposed to the guest content manager.
std::vector<ContentInspection> InspectContent(const Game& game, const fs::path& source,
                                              CheckLevel level);
}  // namespace cave
