#include "content_prepare.h"
#include "run_context.h"
#include <rex/system/xam/content_manager.h>
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace cave::xbox360 {
namespace {
using rex::system::xam::ContentManager;
using rex::system::xam::XCONTENT_AGGREGATE_DATA;
constexpr auto kType=rex::system::XContentType::kMarketplaceContent;
void NoLinks(const fs::path& path,const fs::path& root) {
  for(auto current=path;current!=root;current=current.parent_path()) {
    if(current.empty() || !Within(current,root))throw std::runtime_error("DLC cache path escapes collection");
    const auto attr=GetFileAttributesW(current.c_str());
    if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("DLC cache contains a linked path");
  }
}
std::u16string DisplayName(const ContentPackage& package) {
  // Futari's DLC worker compares XCONTENT_DATA.display_name with wcscmp
  // (0x82192BA8 -> 0x8207AE18). A launcher label is not its native identity.
  const auto wide=FromUtf8(package.native_name.empty()?package.name:package.native_name).native();
  return {wide.begin(),wide.end()};
}
XCONTENT_AGGREGATE_DATA Header(const ContentPackage& package) {
  XCONTENT_AGGREGATE_DATA data{};
  data.device_id=1;data.content_type=kType;data.xuid=0;data.title_id=package.title_id;
  data.set_file_name(package.package_name);data.set_display_name(DisplayName(package));
  return data;
}
}
fs::path PrepareContent(const Game& game,const GamePaths& paths,const fs::path& source,
                        const std::vector<ContentInspection>& inspected) {
  ValidateOutputs(paths);
  if(!source.empty()) {auto guarded=paths;guarded.data=source;ValidateOutputs(guarded);}
  std::vector<std::pair<const ContentPackage*,const ContentInspection*>> selected;
  std::string recipe="evac-content-1\n";
  for(const auto& package:game.content) {
    const auto check=std::find_if(inspected.begin(),inspected.end(),[&](const auto& item) {
      return item.id==package.id && item.state==DataState::Verified;
    });
    if(check==inspected.end())continue;
    if(source.empty() || check->directory.empty() || !Within(check->directory,source))
      throw std::runtime_error("Verified DLC has no source directory");
    selected.emplace_back(&package,&*check);
    recipe+=std::string(package.directory)+"\n"+std::string(package.native_name.empty()?package.name:package.native_name)+"\n";
    for(const auto& file:package.files)recipe+=std::string(file.path)+" "+std::string(file.sha256)+"\n";
  }
  const auto container=paths.prepared/"content";
  const auto target=container/FromUtf8(Sha256Text(recipe));
  NoLinks(target,paths.root);fs::create_directories(container);
  const auto valid=[&](const fs::path& root) {
    NoLinks(root,paths.root);
    if(!fs::is_directory(root))return false;
    const auto checks=InspectContent(game,root,CheckLevel::Full);
    ContentManager metadata(nullptr,root);
    for(const auto& [package,input]:selected) {
      (void)input;
      const auto item=std::find_if(checks.begin(),checks.end(),[&](const auto& check) {return check.id==package->id;});
      if(item==checks.end() || item->state!=DataState::Verified || item->directory!=root/FromUtf8(package->directory))return false;
      const auto title=FromUtf8(package->directory).parent_path().parent_path();
      const auto header=root/title/"Headers/00000002"/(std::string(package->package_name)+".header");
      NoLinks(header,paths.root);
      if(!fs::is_regular_file(header) || fs::file_size(header)!=sizeof(XCONTENT_AGGREGATE_DATA))return false;
      XCONTENT_AGGREGATE_DATA data{};
      const auto expected=Header(*package);
      if(metadata.ReadContentHeaderFile(package->package_name,0,package->title_id,kType,data)!=0 ||
          data.device_id!=1 || data.content_type!=kType || data.xuid!=0 || data.title_id!=package->title_id ||
          std::memcmp(data.file_name_raw,expected.file_name_raw,sizeof(data.file_name_raw)) ||
          std::memcmp(&data.display_name_raw,&expected.display_name_raw,sizeof(data.display_name_raw)))return false;
    }
    return true;
  };
  if(valid(target))return target;
  const auto staging=container/("prepare-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  NoLinks(staging,paths.root);
  if(!fs::create_directory(staging))throw std::runtime_error("DLC preparation directory already exists");
  // A failed transaction is left as a diagnostic cache; it is never mounted.
  ContentManager metadata(nullptr,staging);
  for(const auto& [package,input]:selected) {
    for(const auto& file:package->files) {
      const auto from=DataChild(input->directory,Utf8(ContentRelativePath(*package,file)));
      const auto to=DataChild(staging,file.path);
      NoLinks(from,source);NoLinks(to,paths.root);
      fs::create_directories(to.parent_path());fs::copy_file(from,to);
      if(fs::file_size(to)!=file.bytes || Sha256(to)!=file.sha256)
        throw std::runtime_error("DLC changed during preparation: "+Utf8(from));
    }
    const auto data=Header(*package);
    if(metadata.WriteContentHeaderFile(0,data)!=0)throw std::runtime_error("Cannot prepare DLC metadata");
  }
  if(!valid(staging))throw std::runtime_error("DLC preparation validation failed");
  NoLinks(target,paths.root);
  if(fs::exists(target)) {
    // Keep the exact old cache recoverable; never remove a source extraction.
    const auto previous=container/("replaced-"+staging.filename().string());
    fs::rename(target,previous);
  }
  fs::rename(staging,target);
  return target;
}
}
