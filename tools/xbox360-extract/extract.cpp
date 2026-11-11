#include "extract.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace xboxextract {
namespace {
constexpr uint64_t sector_size = 2048;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void CheckCancel(const std::atomic_bool& cancel) {
  if (cancel.load(std::memory_order_relaxed)) throw std::runtime_error("Extraction cancelled.");
}
uint32_t Number(std::span<const unsigned char> data, size_t at, unsigned count) {
  Require(at <= data.size() && count <= data.size() - at, "Invalid disc directory entry.");
  uint32_t result{};
  for (unsigned i = 0; i < count; ++i) result |= uint32_t(data[at+i]) << (8*i);
  return result;
}
struct OrdinalLess {
  bool operator()(const std::wstring& a, const std::wstring& b) const {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
  }
};
std::wstring Filename(std::span<const unsigned char> bytes) {
  const auto size = static_cast<int>(bytes.size());
  const auto raw = reinterpret_cast<const char*>(bytes.data());
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, size, nullptr, 0);
  Require(length > 0, "Unsupported filename encoding in the ISO.");
  std::wstring name(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, size, name.data(), length);
  Require(name != L"." && name != L".." && name.back() != L'.' && name.back() != L' ', "Unsafe filename in the ISO.");
  for (auto c : name) Require(c >= 32 && std::wstring_view(L"<>:\"/\\|?*").find(c) == std::wstring_view::npos, "Unsafe filename in the ISO.");
  auto stem = name.substr(0, name.find(L'.'));
  std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) { return std::towupper(c); });
  const bool device = stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
    (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
      ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3'));
  Require(!device, "Reserved Windows filename in the ISO.");
  return name;
}
void NoLinks(const fs::path& path) {
  for (auto part = path; !part.empty();) {
    const auto flags = GetFileAttributesW(part.c_str());
    Require(flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_REPARSE_POINT), "Choose an output folder without linked directories.");
    auto parent = part.parent_path();
    if (parent == part) break;
    part = parent;
  }
}
struct Extent { uint64_t offset{}, bytes{}; };
struct Entry { fs::path name; Extent extent; bool directory; };
class Disc {
 public:
  explicit Disc(const fs::path& path) : size_(fs::file_size(path)), input_(path, std::ios::binary) {
    Require(bool(input_), "Cannot open the Xbox 360 ISO.");
    constexpr std::string_view signature = "MICROSOFT*XBOX*MEDIA";
    for (const uint64_t partition : {0ull, 0xfd90000ull, 0x2080000ull, 0x18300000ull}) {
      const auto at = partition + 32*sector_size;
      if (at + sector_size > size_) continue;
      std::array<unsigned char, sector_size> volume{};
      Read(at, volume);
      if (!std::equal(signature.begin(), signature.end(), volume.begin()) ||
          !std::equal(signature.begin(), signature.end(), volume.end()-signature.size())) continue;
      partition_ = partition;
      root_ = Sector(Number(volume, 20, 4), Number(volume, 24, 4));
      return;
    }
    throw std::runtime_error("No Xbox game filesystem found. Select an extracted .iso file, not an archive or XBLA package.");
  }
  void Read(uint64_t at, std::span<unsigned char> bytes) {
    Require(at <= size_ && bytes.size() <= size_-at, "The ISO is truncated or has an invalid file extent.");
    input_.seekg(static_cast<std::streamoff>(at));
    Require(bool(input_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Cannot read the ISO.");
  }
  std::vector<Entry> Entries(const std::atomic_bool& cancel) {
    std::vector<Entry> result;
    Walk(root_, {}, result, cancel, 0);
    Require(std::any_of(result.begin(), result.end(), [](const Entry& e) {
      return !e.directory && _wcsicmp(e.name.c_str(), L"default.xex") == 0;
    }), "This is not an Xbox 360 game disc (default.xex is missing).");
    return result;
  }
 private:
  Extent Sector(uint32_t sector, uint32_t bytes) const {
    const uint64_t offset = partition_ + uint64_t(sector)*sector_size;
    Require(offset <= size_ && bytes <= size_-offset, "The ISO is truncated or has an invalid file extent.");
    return {offset, bytes};
  }
  void Walk(Extent directory, const fs::path& parent, std::vector<Entry>& result,
            const std::atomic_bool& cancel, unsigned depth) {
    CheckCancel(cancel);
    Require(depth <= 64, "Disc directory nesting is too deep.");
    if (!directory.bytes) return;  // Empty directories have no tree node.
    Require(directory.bytes >= 14 && directory.bytes <= 4*1024*1024 && directories_.insert(directory.offset).second,
            "Invalid or cyclic disc directory.");
    std::vector<unsigned char> table(static_cast<size_t>(directory.bytes));
    Read(directory.offset, table);
    std::vector<size_t> pending{0};
    std::map<size_t,size_t> ranges;
    while (!pending.empty()) {
      CheckCancel(cancel);
      const auto at = pending.back(); pending.pop_back();
      Require(at <= table.size()-14, "Invalid disc directory tree.");
      const auto length = table[at+13];
      Require(length && length <= table.size()-at-14, "Invalid disc filename.");
      const size_t end = at+14+length;
      auto after = ranges.lower_bound(at);
      Require((after == ranges.end() || after->first >= end) &&
              (after == ranges.begin() || std::prev(after)->second <= at), "Overlapping or cyclic disc directory tree.");
      ranges.emplace(at, end);
      const auto name = parent / Filename(std::span(table).subspan(at+14,length));
      Require(names_.insert(name.generic_wstring()).second, "Duplicate file path in the ISO.");
      Require(result.size() < 250000, "The ISO contains too many files.");
      const auto extent = Sector(Number(table,at+4,4), Number(table,at+8,4));
      const bool folder = (table[at+12] & 0x10) != 0;
      result.push_back({name, extent, folder});
      if (folder) Walk(extent, name, result, cancel, depth+1);
      for (const auto link : {Number(table,at,2), Number(table,at+2,2)})
        if (link) pending.push_back(size_t(link)*4);
    }
  }
  uint64_t size_, partition_{};
  std::ifstream input_;
  Extent root_;
  std::set<uint64_t> directories_;
  std::set<std::wstring,OrdinalLess> names_;
};
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
}

fs::path Extract(const Options& options, const ProgressCallback& progress, const std::atomic_bool& cancel) {
  Require(!options.input.empty() && !options.output.empty(), "Select an ISO and output folder.");
  const auto output = fs::absolute(options.output).lexically_normal();
  NoLinks(output);
  Require(!fs::exists(output), "The output folder already exists. Choose a new folder.");
  Require(fs::is_directory(output.parent_path()), "The output parent folder does not exist.");
  CheckCancel(cancel);
  Disc disc(options.input);
  const auto entries = disc.Entries(cancel);
  Progress state;
  for (const auto& entry : entries) if (!entry.directory) state.total += entry.extent.bytes;
  Require(fs::space(output.parent_path()).available >= state.total, "Not enough free space in the output location.");
  GUID guid{}; Require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create a temporary folder name.");
  wchar_t id[40]{}; StringFromGUID2(guid, id, 40);
  const auto stage = output.parent_path() / (L".xbox360-extract-" + std::wstring(id));
  Require(fs::create_directory(stage), "Cannot create the extraction folder.");
  // Every cleanup path is the unique, absolute sibling created above, never
  // the user's destination. Only the files created by this invocation are owned.
  std::vector<fs::path> owned{stage};
  try {
    std::vector<unsigned char> buffer(1024*1024);
    auto reported = std::chrono::steady_clock::time_point{};
    for (const auto& entry : entries) {
      CheckCancel(cancel);
      const auto target = stage / entry.name;
      NoLinks(target.parent_path());
      if (entry.directory) {
        Require(fs::create_directory(target), "Cannot create a disc directory.");
        owned.push_back(target);
        continue;
      }
      Handle file{CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
      Require(file.value != INVALID_HANDLE_VALUE, "Cannot create an extracted file.");
      owned.push_back(target);
      state.file = entry.name.generic_wstring();
      for (uint64_t copied = 0; copied < entry.extent.bytes;) {
        CheckCancel(cancel);
        const auto count = static_cast<size_t>(std::min<uint64_t>(buffer.size(), entry.extent.bytes-copied));
        disc.Read(entry.extent.offset+copied, std::span(buffer).first(count));
        DWORD written{};
        Require(WriteFile(file.value, buffer.data(), static_cast<DWORD>(count), &written, nullptr) && written == count, "Cannot write an extracted file. Check free space and folder permissions.");
        copied += count; state.done += count;
        const auto now = std::chrono::steady_clock::now();
        if (progress && now-reported >= std::chrono::milliseconds(100)) { progress(state); reported = now; }
      }
    }
    CheckCancel(cancel);
    NoLinks(output.parent_path());
    // MoveFile does not replace a destination created while extraction ran.
    Require(MoveFileW(stage.c_str(), output.c_str()) != 0, "Cannot publish extraction. The destination may already exist.");
  } catch (...) {
    for (auto i = owned.rbegin(); i != owned.rend(); ++i) { std::error_code error; fs::remove(*i, error); }
    throw;
  }
  state.done = state.total; state.file = L"Complete";
  // A UI notification failure must not undo an already published directory.
  if (progress) { try { progress(state); } catch (...) {} }
  return output;
}
}
