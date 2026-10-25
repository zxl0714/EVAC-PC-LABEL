#include "common/catalogue.h"
#include "common/paths.h"
#include "common/stfs.h"
#include "catalogue_generated.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
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
    case DataState::Nested: return "nested_data";
  }
  return "unknown";
}
namespace {
std::string_view RootEntry(const Game& game) {
  for(const auto& file:game.files)
    if(file.program && (file.path=="default.xex" || file.path=="eboot.bin"))return file.path;
  return {};
}
std::vector<std::string> NestedRoots(const fs::path& root,std::string_view entry) {
  std::vector<std::string> result;
  if(entry.empty() || !fs::is_directory(root))return result;
  const auto finish=[&] {std::sort(result.begin(),result.end());return result;};
  struct Folder {fs::path path;unsigned depth;};
  std::vector<Folder> folders{{root,0}};
  unsigned examined=0;
  // A hint must not become an unbounded disk scan or traverse downloaded links.
  // Look at two wrapper levels, at most 64 folders and 512 directory entries.
  for(size_t next=0;next<folders.size() && next<64;++next) {
    const auto folder=folders[next];
    std::error_code error;
    fs::directory_iterator iterator(folder.path,fs::directory_options::skip_permission_denied,error),end;
    for(;!error && iterator!=end;iterator.increment(error)) {
      if(++examined>512)return finish();
      const auto& item=*iterator;
      const auto attributes=GetFileAttributesW(item.path().c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
         !(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
      const auto raw_candidate=item.path()/FromUtf8(entry);
      const auto candidate_attributes=GetFileAttributesW(raw_candidate.c_str());
      if(candidate_attributes!=INVALID_FILE_ATTRIBUTES &&
         !(candidate_attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        const auto candidate=DataChild(root,Utf8(raw_candidate.lexically_relative(root)));
        result.push_back(Utf8(candidate.parent_path()));
      }
      if(result.size()>=4)return finish();
      if(folder.depth+1<2 && folders.size()<64)folders.push_back({item.path(),folder.depth+1});
    }
  }
  return finish();
}
}
const DataVariant* IdentifyDataVariant(const Game& game,const fs::path& data,uint32_t* hashes) {
  if(game.variants.empty())return nullptr;
  const auto path=DataChild(data,game.variants.front().entry);
  if(!fs::is_regular_file(path))return nullptr;
  const auto bytes=fs::file_size(path);
  std::string digest;
  for(const auto& variant:game.variants) {
    const auto entry=std::find_if(variant.files.begin(),variant.files.end(),[&](const auto& file) {return file.path==variant.entry;});
    if(entry==variant.files.end() || entry->bytes!=bytes)continue;
    if(digest.empty()) {digest=Sha256(path);if(hashes)++*hashes;}
    if(digest==entry->sha256 && fs::file_size(path)==bytes)return &variant;
  }
  return nullptr;
}
std::span<const DataFile> DataFiles(const Game& game,std::string_view variant) {
  if(game.variants.empty() && variant.empty())return game.files;
  for(const auto& item:game.variants)if(item.id==variant)return item.files;
  throw std::runtime_error("Unknown game-data variant");
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
    const auto* variant=IdentifyDataVariant(game,paths.data,&result.hashes_checked);
    const auto files=variant?variant->files:game.files;
    const auto forbidden=variant?variant->forbidden_files:game.forbidden_files;
    if(variant) {result.variant=variant->id;result.version=variant->version;result.layout=variant->layout;}
    uint32_t present = 0;
    bool incomplete = false, unsupported = !game.variants.empty() && !variant &&
        fs::is_regular_file(DataChild(paths.data,game.variants.front().entry));
    if(unsupported)result.problems.push_back("unrecognized root program version: "+std::string(game.variants.front().entry));
    for (const auto relative : forbidden) {
      if (fs::exists(DataChild(paths.data, relative))) {
        unsupported = true;
        result.problems.push_back("unsupported companion: " + std::string(relative));
      }
    }
    for (const auto& expected : files) {
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
    const auto entry=RootEntry(game);
    if(!unsupported && !entry.empty() && !fs::is_regular_file(DataChild(paths.data,entry))) {
      result.candidate_roots=NestedRoots(paths.data,entry);
      if(!result.candidate_roots.empty()) {
        result.reason="Select the extracted game root containing "+std::string(entry)+"; nested entries are not yet verified.";
        result.problems.insert(result.problems.begin(),"select directory: "+result.candidate_roots.front());
      }
    }
    if (!result.candidate_roots.empty()) result.data_state = DataState::Nested;
    else if (unsupported) result.data_state = DataState::Unsupported;
    else if (!present) result.data_state = DataState::Missing;
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
fs::path ContentRelativePath(const ContentPackage& package, const DataFile& file) {
  const auto prefix=std::string(package.directory)+"/";
  if (!file.path.starts_with(prefix)) throw std::runtime_error("Content manifest file is outside its package");
  return FromUtf8(file.path.substr(prefix.size()));
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
      auto less = [](const fs::path& a, const fs::path& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
      };
      const auto unlinked=[&](const fs::path& path) {
        for(auto current=path;current!=source;current=current.parent_path()) {
          if(current.empty() || !Within(current,source))throw std::runtime_error("DLC path escapes source");
          const auto attributes=GetFileAttributesW(current.c_str());
          if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Linked DLC file or directory");
        }
      };
      const auto canonical=FromUtf8(package.directory);
      std::vector<fs::path> candidates;
      std::set<fs::path,decltype(less)> seen(less);
      const auto add=[&](fs::path path) {path=path.lexically_normal();if(seen.insert(path).second)candidates.push_back(std::move(path));};
      add(source/canonical);
      add(source/FromUtf8(package.package_name));add(source);
      // Fixed-depth discovery covers extracted STFS folders and Xbox content
      // trees without searching the drive or relying on a container's filename.
      const auto title=canonical.parent_path().parent_path().filename();
      const std::array parents{source,source/"00000002",source/title/"00000002",source/canonical.parent_path()};
      for(const auto& parent:parents) {
        unlinked(parent);
        if(!fs::is_directory(parent))continue;
        unsigned count=0;
        for(const auto& entry:fs::directory_iterator(parent)) {
          if(++count>64)throw std::runtime_error("Too many DLC folders; select a more specific DLC directory");
          const auto attributes=GetFileAttributesW(entry.path().c_str());
          if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
             (entry.is_directory() || entry.is_regular_file()))add(entry.path());
        }
      }
      unsigned best_present=0;
      for(const auto& directory:candidates) {
        unlinked(directory);
        if(fs::is_regular_file(directory)) {
          auto candidate=InspectStfs(package,directory,level);
          if(candidate.state==DataState::Verified || candidate.state==DataState::Unverified){result=std::move(candidate);break;}
          if(candidate.state!=DataState::Missing && result.state==DataState::Missing)result=std::move(candidate);
          continue;
        }
        if(!fs::is_directory(directory))continue;
        unsigned present=0;
        for(const auto& expected:package.files) {
          const auto file=directory/ContentRelativePath(package,expected);unlinked(file);
          if(fs::is_regular_file(file))++present;
        }
        if(!present && directory!=source/canonical)continue;
        ContentInspection candidate;candidate.id=package.id;candidate.name=package.name;candidate.directory=directory;
        std::set<fs::path,decltype(less)> expected_paths(less);
        for(const auto& expected:package.files) {
          ++candidate.files_checked;
          auto relative=ContentRelativePath(package,expected);
          const auto file=DataChild(directory,Utf8(relative));
          expected_paths.insert(relative.make_preferred());
          if(!fs::is_regular_file(file)) {candidate.problems.push_back("missing: "+Utf8(file));continue;}
          if(fs::file_size(file)!=expected.bytes) {candidate.problems.push_back("size: "+Utf8(file));continue;}
          if(level==CheckLevel::Full) {
            ++candidate.hashes_checked;
            if(Sha256(file)!=expected.sha256 || fs::file_size(file)!=expected.bytes)candidate.problems.push_back("sha256: "+Utf8(file));
          }
        }
        unsigned entries=0;
        for(const auto& entry:fs::recursive_directory_iterator(directory)) {
          if(++entries>1024)throw std::runtime_error("Too many files in DLC payload directory");
          unlinked(entry.path());
          if(!entry.is_directory() && !expected_paths.contains(entry.path().lexically_relative(directory).make_preferred()))
            candidate.problems.push_back("unexpected: "+Utf8(entry.path()));
        }
        candidate.state=candidate.problems.empty()?(level==CheckLevel::Full?DataState::Verified:DataState::Unverified):DataState::Incomplete;
        if(candidate.problems.empty()) {result=std::move(candidate);break;}
        if(result.state==DataState::Missing || present>best_present) {result=std::move(candidate);best_present=present;}
      }
      if(result.state==DataState::Missing && !package.files.empty()) {
        // Bounded structural hints for extra extraction wrappers. These paths
        // are not accepted or mounted until the player selects and verifies one.
        unsigned entries=0;
        for(auto it=fs::recursive_directory_iterator(source);it!=fs::recursive_directory_iterator() && entries<256;++it,++entries) {
          const auto attributes=GetFileAttributesW(it->path().c_str());
          if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {it.disable_recursion_pending();continue;}
          if(it.depth()>=5)it.disable_recursion_pending();
          if(!it->is_directory() || seen.contains(it->path().lexically_normal()))continue;
          const auto marker=it->path()/ContentRelativePath(package,package.files.front());
          unlinked(marker);
          if(!fs::is_regular_file(marker))continue;
          result.state=DataState::Nested;result.directory=it->path();
          result.problems.push_back("Set [game."+std::string(game.id)+"] dlc_dir to: "+Utf8(it->path()));
          break;
        }
      }
    } catch (const std::exception& error) {
      result.state = DataState::Invalid; result.problems.push_back(error.what());
    }
    results.push_back(std::move(result));
  }
  return results;
}
}  // namespace cave
