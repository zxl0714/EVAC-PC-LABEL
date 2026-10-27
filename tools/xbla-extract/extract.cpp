#include "extract.h"
#include "common/stfs.h"
#include <Windows.h>
#include <objbase.h>
#include <stdexcept>

namespace xblaextract {
void Extract(const std::filesystem::path& input, const std::filesystem::path& output) {
  namespace fs = std::filesystem;
  if (input.empty() || output.empty()) throw std::runtime_error("Choose a package and output folder.");
  const auto target = fs::absolute(output).lexically_normal();
  if (fs::exists(target)) throw std::runtime_error("Output folder already exists. Choose a new folder.");
  if (!fs::is_directory(target.parent_path())) throw std::runtime_error("Choose an existing parent folder.");
  cave::StfsPackage package(fs::absolute(input), true, 0xD0000);
  GUID id{};
  if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create extraction identity.");
  wchar_t suffix[40]{};
  StringFromGUID2(id, suffix, 40);
  const auto staging = target.parent_path() / (target.filename().wstring() + L".partial-" + suffix);
  // A failure leaves only a uniquely named partial folder, never a published
  // game directory. No recursive cleanup can affect pre-existing user files.
  package.Extract(staging);
  fs::rename(staging, target);
}
}
