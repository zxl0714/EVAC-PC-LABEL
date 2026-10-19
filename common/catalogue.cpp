#include "common/catalogue.h"
#include "common/paths.h"
#include "catalogue_generated.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cave {
std::span<const Game> Catalogue() { return detail::kCatalogue; }
const Game& FindGame(std::string_view id) {
  for (const auto& game : Catalogue()) if (game.id == id) return game;
  throw std::runtime_error("Unknown game ID: " + std::string(id));
}
std::string Sha256(const fs::path& file) {
  struct Handles {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    ~Handles() {
      if (hash) BCryptDestroyHash(hash);
      if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  } handles;
  if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0) < 0)
    throw std::runtime_error("Cannot initialize SHA-256");
  std::ifstream stream(file, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open file for SHA-256");
  std::array<char, 65536> block;
  while (stream) {
    stream.read(block.data(), block.size());
    if (stream.gcount() && BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(block.data()), ULONG(stream.gcount()), 0) < 0)
      throw std::runtime_error("SHA-256 failed");
  }
  if (!stream.eof()) throw std::runtime_error("File read failed during SHA-256");
  std::array<unsigned char, 32> hash{};
  if (BCryptFinishHash(handles.hash, hash.data(), ULONG(hash.size()), 0) < 0) throw std::runtime_error("SHA-256 finish failed");
  constexpr char digits[] = "0123456789abcdef";
  std::string output;
  for (auto byte : hash) { output += digits[byte >> 4]; output += digits[byte & 15]; }
  return output;
}
std::string_view Name(DataState state) {
  switch (state) {
    case DataState::Missing: return "missing_data";
    case DataState::Incomplete: return "incomplete_data";
    case DataState::Unsupported: return "unsupported_version";
    case DataState::Unverified: return "needs_verification";
    case DataState::Verified: return "verified";
    case DataState::Unknown: return "unknown_layout";
    case DataState::Invalid: return "invalid_path";
  }
  return "unknown";
}
Inspection Inspect(const Game& game, const fs::path& collection_root, const fs::path& data_dir, CheckLevel level) {
  Inspection result;
  result.game = game.id;
  if (game.coming_soon) { result.state = "coming_soon"; result.reason = "Coming Soon"; return result; }
  try {
    const auto paths = Paths(collection_root, game.id, data_dir);
    ValidateOutputs(paths);
    result.data_dir = paths.data;
    if (game.files.empty()) {
      result.state = "not_integrated";
      result.reason = "Backend and data manifest not integrated";
      return result;
    }
    uint32_t present = 0;
    bool incomplete = false, unsupported = false;
    for (const auto& expected : game.files) {
      auto file = DataChild(paths.data, expected.path);
      ++result.files_checked;
      if (!fs::is_regular_file(file)) {
        incomplete = true;
        result.problems.push_back("missing: " + std::string(expected.path));
        continue;
      }
      ++present;
      const auto before = fs::file_size(file);
      if (before != expected.bytes) {
        incomplete = true; unsupported |= expected.program;
        result.problems.push_back("size: " + std::string(expected.path));
        continue;
      }
      if (level == CheckLevel::Full || (level == CheckLevel::Programs && expected.program)) {
        ++result.hashes_checked;
        if (Sha256(file) != expected.sha256 || fs::file_size(file) != before) {
          incomplete = true; unsupported |= expected.program;
          result.problems.push_back("sha256: " + std::string(expected.path));
        }
      }
    }
    if (!present) result.data_state = DataState::Missing;
    else if (unsupported) result.data_state = DataState::Unsupported;
    else if (incomplete) result.data_state = DataState::Incomplete;
    else if (level == CheckLevel::Full) result.data_state = DataState::Verified;
    else result.data_state = DataState::Unverified;
    for (auto relative : game.runtime_files)
      if (!fs::is_regular_file(DataChild(paths.root, relative))) result.missing_runtime.emplace_back(relative);
    if (!game.integrated) { result.state = "not_integrated"; result.reason = "Collection backend not integrated"; }
    else if (result.data_state != DataState::Verified && result.data_state != DataState::Unverified)
      result.state = Name(result.data_state);
    else if (!result.missing_runtime.empty()) result.state = "missing_runtime";
    else result.state = result.data_state == DataState::Verified ? "ready" : "needs_verification";
    if (result.reason.empty()) result.reason = result.state;
  } catch (const std::exception& error) {
    result.state = "invalid_path";
    result.data_state = DataState::Invalid;
    result.reason = error.what();
  }
  return result;
}
std::vector<ContentInspection> InspectContent(const Game& game, const fs::path& requested, CheckLevel level) {
  std::vector<ContentInspection> results;
  for (const auto& package : game.content) {
    ContentInspection result;
    result.id = package.id; result.name = package.name;
    try {
      if (requested.empty() || !fs::exists(requested)) { results.push_back(std::move(result)); continue; }
      const auto source = fs::canonical(requested);
      if (!fs::is_directory(source)) throw std::runtime_error("DLC source must be a directory");
      const auto directory = DataChild(source, package.directory);
      uint32_t present = 0;
      auto less = [](const fs::path& a, const fs::path& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
      };
      std::set<fs::path, decltype(less)> expected_paths(less);
      for (const auto& expected : package.files) {
        ++result.files_checked;
        const auto file = DataChild(source, expected.path);
        expected_paths.insert(FromUtf8(expected.path).make_preferred());
        if (!fs::is_regular_file(file)) { result.problems.push_back("missing: " + std::string(expected.path)); continue; }
        ++present;
        const auto bytes = fs::file_size(file);
        if (bytes != expected.bytes) { result.problems.push_back("size: " + std::string(expected.path)); continue; }
        if (level == CheckLevel::Full) {
          ++result.hashes_checked;
          if (Sha256(file) != expected.sha256 || fs::file_size(file) != bytes)
            result.problems.push_back("sha256: " + std::string(expected.path));
        }
      }
      // A verified directory must not expose additional or linked payloads.
      // The root itself may be an external path alias; links below it are not
      // part of the supported extracted package layout.
      for (auto part = FromUtf8(package.directory); !part.empty(); part = part.parent_path()) {
        const auto path = source / part;
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
          throw std::runtime_error("Linked DLC package directory");
      }
      if (fs::is_directory(directory)) for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
          throw std::runtime_error("Linked or unreadable DLC payload");
        if (!entry.is_directory() && !expected_paths.contains(entry.path().lexically_relative(source).make_preferred()))
          result.problems.push_back("unexpected: " + Utf8(entry.path().lexically_relative(source)));
      }
      // Header files also live outside the payload directory.
      for (const auto& expected : package.files) {
        for (auto part = FromUtf8(expected.path); !part.empty(); part = part.parent_path()) {
          const auto attributes = GetFileAttributesW((source / part).c_str());
          if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Linked DLC file or parent directory");
        }
      }
      if (!present && !fs::exists(directory)) result.state = DataState::Missing;
      else if (!result.problems.empty()) result.state = DataState::Incomplete;
      else result.state = level == CheckLevel::Full ? DataState::Verified : DataState::Unverified;
    } catch (const std::exception& error) {
      result.state = DataState::Invalid; result.problems.push_back(error.what());
    }
    results.push_back(std::move(result));
  }
  return results;
}
}  // namespace cave
