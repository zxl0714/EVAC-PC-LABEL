#include "common/paths.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <stdexcept>

namespace cave {
std::wstring CollectionIdentity(const fs::path& requested) {
  const auto root=fs::canonical(requested);
  HANDLE directory=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                              nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if(directory==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open collection identity");
  std::wstring path(32768,L'\0');
  const auto count=GetFinalPathNameByHandleW(directory,path.data(),DWORD(path.size()),FILE_NAME_NORMALIZED);
  CloseHandle(directory);
  if(!count || count>=path.size())throw std::runtime_error("Cannot resolve collection identity");
  path.resize(count);
  const auto upper_count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),int(path.size()),nullptr,0,nullptr,nullptr,0);
  if(!upper_count)throw std::runtime_error("Cannot normalize collection identity");
  std::wstring upper(upper_count,L'\0');
  if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),int(path.size()),upper.data(),upper_count,nullptr,nullptr,0))
    throw std::runtime_error("Cannot normalize collection identity");
  std::array<unsigned char,32> hash{};
  BCRYPT_ALG_HANDLE algorithm{};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
    throw std::runtime_error("Cannot hash collection identity");
  const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(upper.data()),ULONG(upper.size()*sizeof(wchar_t)),hash.data(),ULONG(hash.size()));
  BCryptCloseAlgorithmProvider(algorithm,0);
  if(status<0)throw std::runtime_error("Cannot hash collection identity");
  constexpr wchar_t digits[]=L"0123456789abcdef";
  std::wstring result;
  for(auto byte:hash) {result+=digits[byte>>4];result+=digits[byte&15];}
  return result;
}
fs::path FromUtf8(std::string_view text) {
  if (text.empty()) return {};
  if (text.size() > 32767 || text.find('\0') != text.npos) throw std::runtime_error("Invalid UTF-8 path");
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
  if (!count) throw std::runtime_error("Invalid UTF-8 text");
  std::wstring value(count, 0);
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), value.data(), count) != count)
    throw std::runtime_error("UTF-8 conversion failed");
  return fs::path(value);
}
std::string Utf8(const fs::path& path) {
  const auto& text = path.native();
  if (text.empty()) return {};
  int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  if (!count) throw std::runtime_error("Invalid Unicode path");
  std::string value(count, 0);
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), value.data(), count, nullptr, nullptr) != count)
    throw std::runtime_error("Unicode conversion failed");
  return value;
}
fs::path Resolve(const fs::path& root, const fs::path& value) {
  if (value.empty()) throw std::runtime_error("Empty path");
  // Do not accept drive-relative C:folder or current-drive-rooted \folder.
  if (value.has_root_path() && !value.is_absolute()) throw std::runtime_error("Use an absolute or root-relative path");
  return fs::weakly_canonical(fs::absolute(value.is_absolute() ? value : root / value));
}
bool Within(const fs::path& child, const fs::path& parent) {
  auto a = child.begin(), b = parent.begin();
  for (; b != parent.end(); ++a, ++b) {
    if (a == child.end()) return false;
    const auto& x = a->native(); const auto& y = b->native();
    if (CompareStringOrdinal(x.data(), int(x.size()), y.data(), int(y.size()), TRUE) != CSTR_EQUAL) return false;
  }
  return true;
}
fs::path DataChild(const fs::path& root, std::string_view relative) {
  auto name = FromUtf8(relative);
  if (name.empty() || name.has_root_path() || relative.find(':') != relative.npos)
    throw std::runtime_error("Invalid manifest path");
  for (const auto& part : name) if (part == ".." || part == ".") throw std::runtime_error("Invalid manifest path component");
  auto result = Resolve(root, name);
  if (!Within(result, root) || result == root) throw std::runtime_error("File link escapes its data/component root");
  return result;
}
GamePaths Paths(const fs::path& root, std::string_view game, const fs::path& data, std::string_view profile) {
  auto token = [](std::string_view value) {
    return !value.empty() && value.size() <= 64 && value.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == value.npos;
  };
  if (!token(game) || !token(profile)) throw std::runtime_error("Invalid game or profile ID");
  auto base = fs::weakly_canonical(fs::absolute(root));
  const auto id = FromUtf8(game);
  return {base, Resolve(base, data), Resolve(base, fs::path("prepared") / id),
          Resolve(base, fs::path("user-data") / id / FromUtf8(profile)),
          Resolve(base, fs::path("logs") / id), Resolve(base, fs::path("cache") / id / "contract-1")};
}
void ValidateOutputs(const GamePaths& paths) {
  const std::array outputs{paths.prepared, paths.profile, paths.logs, paths.cache};
  for (const auto& output : outputs) {
    if (!Within(output, paths.root) || output == paths.root)
      throw std::runtime_error("Writable path redirects outside collection root");
    if (Within(output, paths.data) || Within(paths.data, output))
      throw std::runtime_error("Writable path overlaps read-only game data");
  }
  for (size_t i = 0; i < outputs.size(); ++i) for (size_t j = i + 1; j < outputs.size(); ++j)
    if (Within(outputs[i], outputs[j]) || Within(outputs[j], outputs[i]))
      throw std::runtime_error("Writable paths overlap");
}
}  // namespace cave
