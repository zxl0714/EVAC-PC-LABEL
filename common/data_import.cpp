#include "common/data_import.h"
#include "common/launch.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace cave {
namespace {
struct Entry {fs::path relative;uint64_t bytes;std::string hash;};
bool SamePath(const fs::path& a,const fs::path& b) {return Within(a,b) && Within(b,a);}
void NoLinks(const fs::path& path,const fs::path& root) {
  if (!Within(path.lexically_normal(),root) || SamePath(path.lexically_normal(),root))
    throw std::runtime_error("Copy target must stay inside the collection");
  for (auto item=path;!SamePath(item,root);) {
    const auto attributes=GetFileAttributesW(item.c_str());
    if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Copy paths must not contain file links or directory junctions");
    if (attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)
      throw std::runtime_error("Cannot inspect copy path");
    const auto parent=item.parent_path();
    if(parent.empty() || parent==item)throw std::runtime_error("Copy path cannot reach its collection root");
    item=parent;
  }
}
void EmptyTarget(const fs::path& target) {
  if (fs::exists(target) && (!fs::is_directory(target) || !fs::is_empty(target)))
    throw std::runtime_error("The collection data directory is not empty; existing files were preserved");
}
struct Stage {
  fs::path directory,container,root;
  bool owned{};
  ~Stage() {
    if (!owned) return;
    try {
      // Clean only the exact fresh staging directory owned by this transaction.
      // Refuse cleanup if any path was redirected after creation.
      NoLinks(directory,root);
      const auto actual=fs::canonical(directory);
      if (!SamePath(actual,directory) || !SamePath(actual.parent_path(),container) || !Within(actual,root)) return;
      for (const auto& item:fs::recursive_directory_iterator(actual)) NoLinks(item.path(),root);
      fs::remove_all(actual);
    } catch (...) {} // A retained .copy-* directory is never an installed game.
  }
};
}
CopyResult CopyDataToCollection(const fs::path& requested_root,const Game& game,const fs::path& requested_source,
                                CopyProgress progress,std::function<bool()> cancelled) {
  const auto root=fs::canonical(requested_root),source=fs::canonical(requested_source);
  if (!game.integrated || game.coming_soon || game.files.empty())
    throw std::runtime_error("This game does not have a supported data layout yet");
  if (!fs::is_directory(source)) throw std::runtime_error("Select the original game-data directory");
  NoLinks(root/FromUtf8(game.data_dir),root);
  const auto target=DataChild(root,game.data_dir),container=root/"game-data";
  if (!SamePath(target.parent_path(),container)) throw std::runtime_error("Copy target must be game-data/<game>");
  NoLinks(target,root);
  if (Within(target,source) || Within(source,target))
    throw std::runtime_error("Source and collection copy must not overlap");
  ValidateOutputs(Paths(root,game.id,source));
  ValidateOutputs(Paths(root,game.id,target));
  EmptyTarget(target);
  GameSession session(root);
  auto check=[&] {if(cancelled && cancelled())throw std::runtime_error("Game-data copy cancelled");};
  check();
  const auto inspection=Inspect(game,root,source,CheckLevel::Full);
  if (inspection.data_state!=DataState::Verified) {
    auto reason=inspection.reason;
    if (!inspection.problems.empty())reason+=" / "+inspection.problems.front();
    throw std::runtime_error("Unsupported or incomplete source data: "+reason);
  }
  std::vector<Entry> entries;
  std::vector<fs::path> directories;
  uint64_t total{};
  for (const auto& item:fs::recursive_directory_iterator(source)) {
    check();NoLinks(item.path(),source);
    const auto relative=item.path().lexically_relative(source);
    (void)DataChild(source,Utf8(relative));
    if (item.is_directory()) {directories.push_back(relative);continue;}
    if (!item.is_regular_file()) throw std::runtime_error("Unsupported file type in source data");
    const auto bytes=item.file_size();
    if (bytes>std::numeric_limits<uint64_t>::max()-total)throw std::runtime_error("Copy size exceeds supported bounds");
    total+=bytes;entries.push_back({relative,bytes,Sha256(item.path())});
  }
  if (entries.empty())throw std::runtime_error("The source data directory is empty");
  check();
  std::array<unsigned char,16> nonce{};
  if (BCryptGenRandom(nullptr,nonce.data(),ULONG(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
    throw std::runtime_error("Cannot name copy transaction");
  std::wstring name=L".copy-"+FromUtf8(game.id).native()+L"-";
  for(auto byte:nonce)name+=std::to_wstring(byte)+L"-";
  NoLinks(container,root);fs::create_directories(container);NoLinks(container,root);
  if (fs::space(container).available<total)throw std::runtime_error("Not enough free space for this game-data copy");
  Stage stage{container/name,container,root};
  if (!fs::create_directory(stage.directory))throw std::runtime_error("Copy transaction collision");
  stage.owned=true;
  for (const auto& directory:directories)fs::create_directories(DataChild(stage.directory,Utf8(directory)));
  uint64_t copied{};
  if(progress)progress(0,total);
  for (const auto& entry:entries) {
    check();NoLinks(stage.directory,root);
    const auto input=DataChild(source,Utf8(entry.relative)),output=DataChild(stage.directory,Utf8(entry.relative));
    NoLinks(input,source);NoLinks(output,root);
    fs::create_directories(output.parent_path());
    if (!fs::copy_file(input,output,fs::copy_options::none))throw std::runtime_error("Cannot copy a game-data file");
    // Only the fresh copy is made writable, so a cancelled transaction can be
    // removed even when the source medium marked its files read-only.
    fs::permissions(output,fs::perms::owner_write,fs::perm_options::add);
    if (fs::file_size(input)!=entry.bytes || fs::file_size(output)!=entry.bytes ||
        Sha256(output)!=entry.hash || Sha256(input)!=entry.hash)
      throw std::runtime_error("Game-data copy verification failed; source or output changed");
    copied+=entry.bytes;if(progress)progress(copied,total);
  }
  check();
  if (Inspect(game,root,stage.directory,CheckLevel::Full).data_state!=DataState::Verified)
    throw std::runtime_error("Copied data no longer matches the supported version");
  // Compare exact inventories, not just counts. Re-read all bytes, including
  // extra resources, so a concurrent edit cannot publish a mixed snapshot.
  std::map<fs::path,const Entry*> expected_files;
  for(const auto& entry:entries)expected_files.emplace(entry.relative,&entry);
  const std::set<fs::path> expected_directories(directories.begin(),directories.end());
  for(const auto& directory:{source,stage.directory}) {
    size_t current_files{};std::set<fs::path> current_directories;
    for(const auto& item:fs::recursive_directory_iterator(directory)) {
      check();NoLinks(item.path(),directory);
      const auto relative=item.path().lexically_relative(directory);
      if(item.is_directory()) {current_directories.insert(relative);continue;}
      const auto found=expected_files.find(relative);
      if(!item.is_regular_file() || found==expected_files.end() || item.file_size()!=found->second->bytes ||
         Sha256(item.path())!=found->second->hash)
        throw std::runtime_error("Game-data files changed during copy verification");
      ++current_files;
    }
    if(current_files!=entries.size() || current_directories!=expected_directories)
      throw std::runtime_error("Game-data layout changed during copy verification");
  }
  check();NoLinks(target,root);EmptyTarget(target);
  if (fs::exists(target) && !fs::remove(target))throw std::runtime_error("Cannot use empty data directory");
  // Windows rename refuses an existing target. Never merge or replace files.
  if (!MoveFileExW(stage.directory.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot publish copied game data; existing target was preserved");
  stage.owned=false;
  try {SetDataDirectory(root,game,target);}
  catch(const std::exception& error) {
    throw std::runtime_error("Data copied and verified, but config.ini was not updated: "+std::string(error.what()));
  }
  return {target,uint64_t(entries.size()),total};
}
}
