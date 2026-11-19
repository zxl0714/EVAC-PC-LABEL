#include "common/data_import.h"
#include "common/launch.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
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
constexpr auto kGuide=L"README.txt";
const fs::path kArrange=L"x-arrange";
struct PreservedFile {
  uint64_t bytes{};std::string hash;
  bool operator==(const PreservedFile&) const=default;
};
struct PreservedTarget {
  std::map<fs::path,PreservedFile> files;
  std::set<fs::path> directories;
  bool operator==(const PreservedTarget&) const=default;
};
std::optional<PreservedTarget> AvailableTarget(const fs::path& target,const fs::path& root,std::string_view game) {
  if(!fs::exists(target))return {};
  const bool preserves_arrange=PreservesXArrangeOnImport(game);
  const auto fail=[preserves_arrange] {throw std::runtime_error(preserves_arrange?
      "The import destination must contain only the placement guide or X Arrange folder":
      "The collection data directory is not empty; only a placement guide may be preserved");};
  if(!fs::is_directory(target))fail();
  PreservedTarget preserved;
  for(const auto& item:fs::recursive_directory_iterator(target)) {
    NoLinks(item.path(),root);
    const auto relative=item.path().lexically_relative(target);
    const bool arrange=preserves_arrange && Within(relative,kArrange);
    if(item.is_directory() && arrange) {
      preserved.directories.insert(relative);
      continue;
    }
    if(!item.is_regular_file())fail();
    const auto bytes=item.file_size();
    const bool guide=relative==kGuide && bytes<=1024*1024;
    if(!guide && !(arrange && relative.has_parent_path()))fail();
    preserved.files.emplace(relative,PreservedFile{bytes,Sha256(item.path())});
  }
  if(preserved.files.empty() && preserved.directories.empty())return {};
  return preserved;
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
  (void)AvailableTarget(target,root,game.id);
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
  const bool disc_only=game.backend=="ps2";
  fs::file_time_type disc_source_time{},disc_copy_time{};
  if(disc_only) {
    const auto& image=inspection.disc_image;
    if(image.empty() || !SamePath(image.parent_path(),source))throw std::runtime_error("Missing verified game ISO");
    NoLinks(image,source);
    total=fs::file_size(image);
    disc_source_time=fs::last_write_time(image);
    const auto identity=std::find_if(game.files.begin(),game.files.end(),[](const auto& file){return file.path=="disc.iso" && !file.program;});
    if(identity==game.files.end())throw std::runtime_error("Missing verified disc identity");
    entries.push_back({image.filename(),total,std::string(identity->sha256)});
  } else {
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
        Sha256(output)!=entry.hash || (disc_only?fs::last_write_time(input)!=disc_source_time:Sha256(input)!=entry.hash))
      throw std::runtime_error("Game-data copy verification failed; source or output changed");
    if(disc_only)disc_copy_time=fs::last_write_time(output);
    copied+=entry.bytes;if(progress)progress(copied,total);
  }
  check();
  if (Inspect(game,root,stage.directory,disc_only?CheckLevel::Programs:CheckLevel::Full).data_state!=
      (disc_only?DataState::Unverified:DataState::Verified))
    throw std::runtime_error("Copied data no longer matches the supported version");
  // Compare exact inventories, not just counts. Re-read all bytes, including
  // extra resources, so a concurrent edit cannot publish a mixed snapshot.
  std::map<fs::path,const Entry*> expected_files;
  for(const auto& entry:entries)expected_files.emplace(entry.relative,&entry);
  const std::set<fs::path> expected_directories(directories.begin(),directories.end());
  for(const auto& directory:{source,stage.directory}) {
    if(disc_only && directory==source) {
      const auto current=Inspect(game,root,source,CheckLevel::Programs);
      if(current.disc_image!=inspection.disc_image || current.data_state!=DataState::Unverified ||
         fs::last_write_time(current.disc_image)!=disc_source_time)
        throw std::runtime_error("The source ISO changed during copy verification");
      continue;
    }
    size_t current_files{};std::set<fs::path> current_directories;
    for(const auto& item:fs::recursive_directory_iterator(directory)) {
      check();NoLinks(item.path(),directory);
      const auto relative=item.path().lexically_relative(directory);
      if(item.is_directory()) {current_directories.insert(relative);continue;}
      const auto found=expected_files.find(relative);
      if(!item.is_regular_file() || found==expected_files.end() || item.file_size()!=found->second->bytes ||
         (disc_only?fs::last_write_time(item.path())!=disc_copy_time:Sha256(item.path())!=found->second->hash))
        throw std::runtime_error("Game-data files changed during copy verification");
      ++current_files;
    }
    if(current_files!=entries.size() || current_directories!=expected_directories)
      throw std::runtime_error("Game-data layout changed during copy verification");
  }
  check();NoLinks(target,root);const auto existing=AvailableTarget(target,root,game.id);
  fs::path guide_backup;
  if(existing) {
    for(const auto& relative:existing->directories)fs::create_directories(DataChild(stage.directory,Utf8(relative)));
    for(const auto& [relative,entry]:existing->files) {
      check();
      const auto original=DataChild(target,Utf8(relative)),preserved=DataChild(stage.directory,Utf8(relative));
      if(fs::exists(preserved)) {
        if(!fs::is_regular_file(preserved) || fs::file_size(preserved)!=entry.bytes || Sha256(preserved)!=entry.hash)
          throw std::runtime_error("Source and destination have different files at "+Utf8(relative)+"; existing files were preserved");
      } else if(!fs::copy_file(original,preserved,fs::copy_options::none))throw std::runtime_error("Cannot preserve "+Utf8(relative));
      fs::permissions(preserved,fs::perms::owner_write,fs::perm_options::add);
      if(fs::file_size(preserved)!=entry.bytes || Sha256(preserved)!=entry.hash)
        throw std::runtime_error("Destination file changed during copy; existing files were preserved");
    }
    check();
    if(AvailableTarget(target,root,game.id)!=existing)
      throw std::runtime_error("Destination changed during copy; existing files were preserved");
    // Retain the original destination as a recoverable cache backup. The
    // game-data target is still published with one non-overwriting rename.
    const auto parent=root/"cache"/FromUtf8(game.id)/"contract-1"/"import-guides";
    NoLinks(parent,root);
    if(Within(parent,source) || Within(source,parent))throw std::runtime_error("Guide backup overlaps read-only source data");
    fs::create_directories(parent);NoLinks(parent,root);guide_backup=parent/name;NoLinks(guide_backup,root);
    if(!MoveFileExW(target.c_str(),guide_backup.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve the original destination files");
    try {
      if(AvailableTarget(guide_backup,root,game.id)!=existing)throw std::runtime_error("Destination changed during copy");
      if(!MoveFileExW(stage.directory.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish copied game data");
    } catch(const std::exception& error) {
      if(!MoveFileExW(guide_backup.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(std::string(error.what())+"; original destination retained at "+Utf8(guide_backup));
      throw;
    }
  } else {
    if (fs::exists(target) && !fs::remove(target))throw std::runtime_error("Cannot use empty data directory");
  // Windows rename refuses an existing target. Never merge or replace files.
    if (!MoveFileExW(stage.directory.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("Cannot publish copied game data; existing target was preserved");
  }
  stage.owned=false;
  try {SetDataDirectory(root,game,target);}
  catch(const std::exception& error) {
    throw std::runtime_error("Data copied and verified, but config.ini was not updated: "+std::string(error.what()));
  }
  return {target,uint64_t(entries.size()),total,guide_backup};
}
}
