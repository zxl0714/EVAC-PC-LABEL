#include "launcher/program_update.h"
#include "launcher/single_instance.h"
#include "common/catalogue.h"
#include "common/config_update.h"
#include "common/launch.h"
#include "common/paths.h"
#include "common/settings.h"
#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <set>

namespace cave::launcher::update {
namespace {
using Json=nlohmann::json;
constexpr auto kLauncher="EVAC PC LABEL.exe";
constexpr auto kManifest="release.json";
constexpr auto kSums="SHA256SUMS.txt";
constexpr auto kJournal="pending.json";
constexpr size_t kMetadataLimit=2*1024*1024,kFileLimit=4096;
constexpr uint64_t kExpandedLimit=4ull*1024*1024*1024;

struct Handle {
  HANDLE value{};
  ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct HttpHandle {
  HINTERNET value{};
  explicit HttpHandle(HINTERNET input):value(input){if(!value)throw std::runtime_error("Cannot open GitHub download");}
  ~HttpHandle(){WinHttpCloseHandle(value);}
};
void Require(bool okay,const char* message) {
  if(!okay)throw std::runtime_error(message);
}
void Move(const fs::path& from,const fs::path& to) {
  if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot replace "+Utf8(to.filename())+" (Windows "+std::to_string(GetLastError())+")");
}
std::string Lower(std::string value) {
  for(auto& c:value)if(c>='A' && c<='Z')c=char(c-'A'+'a');
  return value;
}
bool SafePart(std::string_view part) {
  if(part.empty() || part.back()==' ' || part.back()=='.' ||
     part.find_first_of("\\:\"<>|?*")!=part.npos ||
     std::ranges::any_of(part,[](unsigned char c){return c<32 || c==127;}))return false;
  const auto base=Lower(std::string(part.substr(0,part.find('.'))));
  if(base=="con" || base=="prn" || base=="aux" || base=="nul")return false;
  return !(base.size()==4 && (base.starts_with("com") || base.starts_with("lpt")) && base.back()>='0' && base.back()<='9');
}
fs::path LocalPath(const fs::path& base,std::string_view relative) {
  Require(!relative.empty() && relative.size()<=350,"Invalid update file path");
  auto path=base;
  for(;;) {
    const auto slash=relative.find('/');
    const auto part=relative.substr(0,slash);
    Require(SafePart(part),"Invalid update path component");
    path/=FromUtf8(part);
    const auto flags=GetFileAttributesW(path.c_str());
    if(flags!=INVALID_FILE_ATTRIBUTES)Require(!(flags&FILE_ATTRIBUTE_REPARSE_POINT),"Update path contains a link");
    if(slash==relative.npos)break;
    if(flags!=INVALID_FILE_ATTRIBUTES)Require(flags&FILE_ATTRIBUTE_DIRECTORY,"Update parent is not a directory");
    relative.remove_prefix(slash+1);
  }
  return path;
}
std::optional<fs::path> GameGuideRoot(std::string_view name) {
  if(name=="game-data/daioujou/x-arrange/README.txt")return fs::path("game-data/daioujou");
  if(name=="game-data/ketsui/x-arrange/README.txt")return fs::path("game-data/ketsui");
  for(const auto& game:Catalogue()) {
    const auto directory="game-data/"+std::string(game.id);
    if(name==directory+"/README.txt")return FromUtf8(directory);
  }
  return std::nullopt;
}
bool Managed(std::string_view name,std::string_view role) {
  if(role!="runtime" && role!="redist" && role!="documentation" && role!="license" && role!="config")return false;
  if(name.starts_with("runtime/") || name.starts_with("tools/") || name.starts_with("licenses/"))return true;
  if(name.starts_with("game-data/"))return role=="documentation" && GameGuideRoot(name).has_value();
  if(name==kLauncher)return role=="runtime";
  if(name=="Launch.bat" || name=="README.txt" || name=="SOURCE.txt")return role=="documentation";
  return role=="redist" && name.find('/')==name.npos && name.ends_with(".dll");
}
Json ReadJson(const fs::path& path) {
  Require(fs::is_regular_file(path) && fs::file_size(path)<=kMetadataLimit,"Invalid update metadata file");
  std::ifstream input(path,std::ios::binary);
  Require(bool(input),"Cannot read update metadata");
  return Json::parse(input);
}
void WriteJournal(const fs::path& path,const Json& value) {
  const auto text=value.dump();
  Require(text.size()<=kMetadataLimit,"Update journal exceeds its size limit");
  const auto temporary=LocalPath(path.parent_path(),"pending.tmp");
  Handle file{CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
  Require(file.value!=INVALID_HANDLE_VALUE,"Cannot create update journal");
  DWORD written{};
  Require(WriteFile(file.value,text.data(),DWORD(text.size()),&written,nullptr) &&
          written==text.size() && FlushFileBuffers(file.value),"Cannot write update journal");
  CloseHandle(file.value);file.value=nullptr;
  Move(temporary,path);
}
struct PackageFile {std::string name,sha256,role;uint64_t bytes{};};
struct Manifest {std::string version;std::vector<PackageFile> files;};
Manifest ReadManifest(const fs::path& root) {
  const auto data=ReadJson(LocalPath(root,kManifest));
  const auto format=data.at("format").get<int>();
  Require((format==2 || format==3) && data.at("flavor")=="Public" && data.at("platform")=="win-x64" &&
          data.at("archiveLayout")=="flat" && data.at("sourceIncluded")==false &&
          data.at("dataGames").is_array() && data.at("dataGames").empty() &&
          data.at("futariContent").is_array() && data.at("futariContent").empty() &&
          data.at("symbolsIncluded")==false,"Expected a flat Windows program package");
  Manifest manifest{data.at("version").get<std::string>(),{}};
  Require(ParseVersion(manifest.version).has_value(),"Invalid package version");
  const auto& files=data.at("files");
  Require(files.is_array() && !files.empty() && files.size()<=kFileLimit,"Invalid package file inventory");
  std::set<std::string> names;
  uint64_t total{};
  for(const auto& row:files) {
    PackageFile entry{row.at("path").get<std::string>(),row.at("sha256").get<std::string>(),
                      row.at("role").get<std::string>(),row.at("bytes").get<uint64_t>()};
    Require(row.at("bytes").is_number_unsigned() && entry.bytes<=kExpandedLimit-total &&
            ValidSha256(entry.sha256) && Managed(entry.name,entry.role) &&
            names.insert(Lower(entry.name)).second,"Invalid or unmanaged package file");
    (void)LocalPath(root,entry.name);
    total+=entry.bytes;
    manifest.files.push_back(std::move(entry));
  }
  Require(std::ranges::find(manifest.files,kLauncher,&PackageFile::name)!=manifest.files.end(),"Package launcher is missing");
  return manifest;
}
std::vector<PackageFile> VerifyPackage(const fs::path& root,std::string_view tag) {
  auto manifest=ReadManifest(root);
  Require(ParseVersion(manifest.version)==ParseVersion(tag),"Package version differs from the selected release");
  std::set<std::string> names{kManifest,kSums};
  for(const auto& file:manifest.files) {
    const auto path=LocalPath(root,file.name);
    Require(fs::is_regular_file(path) && fs::file_size(path)==file.bytes && Sha256(path)==file.sha256,
            "Extracted package file differs from its manifest");
    names.insert(file.name);
  }
  for(const auto& entry:fs::recursive_directory_iterator(root)) {
    const auto relative=Utf8(fs::path(entry.path().lexically_relative(root).generic_wstring()));
    const auto path=LocalPath(root,relative);
    if(entry.is_directory())continue;
    Require(fs::is_regular_file(path) && names.erase(relative)==1,"Unexpected file in program archive");
  }
  Require(names.empty(),"Program archive is incomplete");
  for(const auto* name:{kManifest,kSums}) {
    const auto path=LocalPath(root,name);
    Require(fs::file_size(path)<=kMetadataLimit,"Program archive metadata is too large");
    manifest.files.push_back({name,Sha256(path),{},fs::file_size(path)});
  }
  // Keep an executable launcher available throughout installation and recovery.
  std::stable_partition(manifest.files.begin(),manifest.files.end(),[](const auto& file){return file.name!=kLauncher;});
  return std::move(manifest.files);
}
fs::path ArchivePath(const fs::path& root,const Release& release) {
  Require(TrustedProgram(release),"Release has no verified Windows program asset");
  return LocalPath(UpdateStorage(root),release.tag+"-"+release.program->sha256+".zip");
}
bool ArchiveMatches(const fs::path& path,const ProgramAsset& asset) {
  return fs::is_regular_file(path) && fs::file_size(path)==asset.bytes && Sha256(path)==asset.sha256;
}
bool DownloadHost(std::wstring_view host) {
  return host==L"github.com" || host==L"release-assets.githubusercontent.com" || host==L"objects.githubusercontent.com";
}
void FetchArchive(const ProgramAsset& asset,const fs::path& file) {
  HttpHandle session(WinHttpOpen(L"EVAC-PC-LABEL-program/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
  Require(WinHttpSetTimeouts(session.value,5000,5000,15000,15000),"Cannot set download timeouts");
  auto url=FromUtf8(asset.url).native();
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(45);
  for(int redirects=0;redirects<5;++redirects) {
    URL_COMPONENTSW parsed{};parsed.dwStructSize=sizeof(parsed);
    parsed.dwHostNameLength=parsed.dwUrlPathLength=parsed.dwExtraInfoLength=DWORD(-1);
    parsed.dwUserNameLength=parsed.dwPasswordLength=DWORD(-1);
    Require(WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&parsed) &&
            parsed.nScheme==INTERNET_SCHEME_HTTPS && parsed.nPort==INTERNET_DEFAULT_HTTPS_PORT &&
            !parsed.dwUserNameLength && !parsed.dwPasswordLength && parsed.dwUrlPathLength,
            "Invalid GitHub download redirect");
    const std::wstring host(parsed.lpszHostName,parsed.dwHostNameLength);
    Require(DownloadHost(host),"Unexpected GitHub download host");
    std::wstring target(parsed.lpszUrlPath,parsed.dwUrlPathLength);
    if(parsed.dwExtraInfoLength)target.append(parsed.lpszExtraInfo,parsed.dwExtraInfoLength);
    HttpHandle connection(WinHttpConnect(session.value,host.c_str(),parsed.nPort,0));
    HttpHandle request(WinHttpOpenRequest(connection.value,L"GET",target.c_str(),nullptr,
                        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_REDIRECTS;
    Require(WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
            WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) &&
            WinHttpReceiveResponse(request.value,nullptr),"GitHub download connection failed");
    DWORD status{},size=sizeof(status);
    Require(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Cannot read download status");
    if(status>=300 && status<400) {
      std::array<wchar_t,8192> location{};size=DWORD(sizeof(location));
      Require(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,
              location.data(),&size,WINHTTP_NO_HEADER_INDEX),"GitHub download redirect is missing");
      url.assign(location.data(),size/sizeof(wchar_t));
      while(!url.empty() && url.back()==0)url.pop_back();
      continue;
    }
    if(status!=200)throw std::runtime_error("GitHub download HTTP "+std::to_string(status));
    Handle output{CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Require(output.value!=INVALID_HANDLE_VALUE,"Cannot create partial update download");
    std::array<unsigned char,128*1024> buffer{};
    uint64_t received{};
    for(;;) {
      DWORD read{},written{};
      Require(std::chrono::steady_clock::now()<deadline &&
              WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&read),"Program download interrupted");
      if(!read)break;
      Require(read<=asset.bytes-received && WriteFile(output.value,buffer.data(),read,&written,nullptr) &&
              read==written,"Program download exceeds its size or cannot be written");
      received+=read;
    }
    Require(received==asset.bytes && FlushFileBuffers(output.value),"Program download is incomplete");
    return;
  }
  throw std::runtime_error("Too many GitHub download redirects");
}
Handle Spawn(const fs::path& executable,std::wstring_view arguments,const fs::path& root) {
  auto command=QuoteArgument(executable.native())+L" "+std::wstring(arguments);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);
  PROCESS_INFORMATION process{};
  Require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
                         root.c_str(),&startup,&process)!=FALSE,"Cannot start update process");
  CloseHandle(process.hThread);
  return {process.hProcess};
}
void Extract(const fs::path& archive,const fs::path& destination) {
  std::array<wchar_t,MAX_PATH> system{};
  const auto length=GetSystemDirectoryW(system.data(),DWORD(system.size()));
  Require(length && length<system.size(),"Cannot locate Windows tar");
  const auto tar=fs::path(system.data())/"tar.exe";
  const auto command=L"-xf "+QuoteArgument(archive.native())+L" -C "+QuoteArgument(destination.native());
  const auto process=Spawn(tar,command,destination);
  DWORD code{};
  Require(WaitForSingleObject(process.value,INFINITE)==WAIT_OBJECT_0 &&
          GetExitCodeProcess(process.value,&code) && !code,"Cannot extract the verified program ZIP");
}
void ResetTransaction(const fs::path& storage) {
  const auto path=LocalPath(storage,"transaction");
  if(!fs::exists(path))return;
  const auto resolved=fs::canonical(path);
  Require(Within(resolved,storage) && resolved!=storage,"Invalid update transaction directory");
  fs::remove_all(resolved);
}
std::vector<fs::path> DataDirectories(const fs::path& root) {
  const auto ini=Ini::Load(ConfigPath(root));
  std::vector<fs::path> directories;
  for(const auto& game:Catalogue()) {
    const auto data=Resolve(root,ini.DataDirectory(game));
    const auto content=ini.ContentDirectory(game,data);
    directories.push_back(data);
    if(!content.empty())directories.push_back(Resolve(root,content));
  }
  return directories;
}
void CheckGameFiles(const fs::path& root,const fs::path& target,std::string_view name,
                    const std::vector<fs::path>& directories) {
  const auto guide=GameGuideRoot(name);
  for(const auto& data:directories) {
    if(!Within(target,data) && !Within(data,target))continue;
    const auto canonical_guide=guide && data==root/(*guide);
    Require(canonical_guide,"Program update overlaps configured game data");
  }
}
}

bool PackagedInstallation(const fs::path& root,std::string_view version) noexcept {
  try {
    return fs::is_regular_file(LocalPath(root,kLauncher)) &&
           fs::is_regular_file(LocalPath(root,kSums)) && ParseVersion(ReadManifest(root).version)==ParseVersion(version);
  }catch(...){return false;}
}
bool RecoveryPending(const fs::path& root) {
  if(!fs::exists(root/"user-data/updates/pending.json"))return false;
  return fs::exists(LocalPath(UpdateStorage(root),kJournal));
}
void DownloadProgram(const fs::path& root,const Release& release) {
  const auto destination=ArchivePath(root,release);
  fs::create_directories(destination.parent_path());
  if(ArchiveMatches(destination,*release.program))return;
  const auto partial=LocalPath(destination.parent_path(),"download-"+std::to_string(GetCurrentProcessId())+"-"+
                               std::to_string(GetTickCount64())+".part");
  struct Cleanup {fs::path path;~Cleanup(){DeleteFileW(path.c_str());}} cleanup{partial};
  FetchArchive(*release.program,partial);
  Require(ArchiveMatches(partial,*release.program),"Program ZIP failed SHA-256 verification");
  Move(partial,destination);
}
void RecoverProgram(const fs::path& root) {
  const auto storage=UpdateStorage(root),journal=LocalPath(storage,kJournal);
  if(!fs::exists(journal))return;
  const auto saved=ReadJson(journal);
  const auto& files=saved.at("files");
  Require(saved.at("schema")==1 && files.is_array() && files.size()<=kFileLimit+2,"Invalid update recovery journal");
  const auto directories=DataDirectories(root);
  const auto transaction=LocalPath(storage,"transaction");
  const auto backup=LocalPath(transaction,"backup");
  for(auto it=files.rbegin();it!=files.rend();++it) {
    const auto name=it->at("name").get<std::string>(),incoming=it->at("incoming").get<std::string>(),
               before=it->at("before").get<std::string>(),role=it->at("role").get<std::string>();
    Require((Managed(name,role) || name==kManifest || name==kSums) &&
            ValidSha256(incoming) && (before.empty() || ValidSha256(before)),"Invalid recovery file");
    const auto target=LocalPath(root,name);
    CheckGameFiles(root,target,name,directories);
    const auto installed=fs::is_regular_file(target)?Sha256(target):std::string{};
    if(installed==before)continue;
    Require(installed==incoming,"A program file changed during update recovery");
    if(before.empty()) {
      Require(DeleteFileW(target.c_str())!=FALSE,"Cannot remove a newly installed update file");
    }else{
      const auto original=LocalPath(backup,name);
      Require(fs::is_regular_file(original) && Sha256(original)==before,"Update backup differs from its journal");
      Move(original,target);
    }
  }
  Require(DeleteFileW(journal.c_str())!=FALSE,"Cannot complete update recovery");
  try {ResetTransaction(storage);}catch(...){}
}
void InstallProgram(const fs::path& root,const Release& release) {
  Require(TrustedProgram(release),"Program release is invalid");
  const auto storage=UpdateStorage(root);
  Require(!RecoveryPending(root),"Recover the interrupted update before installing");
  const auto current=ReadManifest(root);
  Require(Newer(current.version,release),"Program update must be newer than the installed version");
  const auto archive=ArchivePath(root,release);
  const Handle archive_lock{CreateFileW(archive.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,nullptr)};
  Require(archive_lock.value!=INVALID_HANDLE_VALUE && ArchiveMatches(archive,*release.program),"Program ZIP failed verification");
  ResetTransaction(storage);
  const auto transaction=LocalPath(storage,"transaction");
  const auto incoming=LocalPath(transaction,"incoming"),backup=LocalPath(transaction,"backup");
  fs::create_directories(incoming);
  Extract(archive,incoming);
  const auto files=VerifyPackage(incoming,release.tag);
  const auto directories=DataDirectories(root);
  std::set<std::string> owned{Lower(kManifest),Lower(kSums)};
  for(const auto& file:current.files)owned.insert(Lower(file.name));
  auto changes=Json::array();
  for(const auto& file:files) {
    const auto target=LocalPath(root,file.name);
    CheckGameFiles(root,target,file.name,directories);
    std::string before;
    if(fs::exists(target)) {
      Require(fs::is_regular_file(target),"Program update destination is not a file");
      before=Sha256(target);
      if(before==file.sha256)continue;
      if(!owned.contains(Lower(file.name)))throw std::runtime_error("Program update would overwrite "+file.name);
      const auto original=LocalPath(backup,file.name);
      fs::create_directories(original.parent_path());
      fs::copy_file(target,original);
      Require(Sha256(original)==before,"Installed file changed while making an update backup");
    }
    changes.push_back({{"name",file.name},{"incoming",file.sha256},{"before",before},{"role",file.role}});
  }
  const auto journal=LocalPath(storage,kJournal);
  WriteJournal(journal,Json{{"schema",1},{"files",changes}});
  try {
    for(const auto& entry:changes) {
      const auto& name=entry.at("name").get_ref<const std::string&>();
      const auto target=LocalPath(root,name);
      const auto& before=entry.at("before").get_ref<const std::string&>();
      const auto unchanged=before.empty()?!fs::exists(target):fs::is_regular_file(target) && Sha256(target)==before;
      Require(unchanged,"An installed file changed during the update");
      fs::create_directories(target.parent_path());
      Move(LocalPath(incoming,name),target);
    }
    Require(DeleteFileW(journal.c_str())!=FALSE,"Cannot finish program update");
  }catch(...) {
    RecoverProgram(root);
    throw;
  }
  try {ResetTransaction(storage);}catch(...){}
}
void StartProgramWorker(const fs::path& root,std::wstring_view action) {
  const auto storage=UpdateStorage(root);
  fs::create_directories(storage);
  const auto worker=LocalPath(storage,"apply.exe");
  if(action!=L"recover" || !fs::is_regular_file(worker)) {
    const auto manifest=ReadManifest(root);
    for(const auto& file:manifest.files) {
      if(file.role=="redist" && file.name.find('/')==file.name.npos)
        fs::copy_file(LocalPath(root,file.name),LocalPath(storage,file.name),fs::copy_options::overwrite_existing);
    }
    fs::copy_file(LocalPath(root,kLauncher),worker,fs::copy_options::overwrite_existing);
  }
  const auto arguments=L"--root "+QuoteArgument(root.native())+L" --apply-update "+QuoteArgument(action)+
                       L" --update-parent "+std::to_wstring(GetCurrentProcessId());
  (void)Spawn(worker,arguments,root);
}
int RunProgramWorker(const fs::path& root,std::wstring_view action,unsigned long parent) {
  const Handle previous{OpenProcess(SYNCHRONIZE,FALSE,parent)};
  if(previous.value)Require(WaitForSingleObject(previous.value,15000)==WAIT_OBJECT_0,"The launcher is still closing");
  {
    LauncherInstance instance(root);
    Require(instance.ActivateOrAcquire(),"Close the other launcher before updating");
    GameSession game_session(root);
    if(action==L"recover")RecoverProgram(root);
    else {
      const auto state=Load(root);
      Require(state.latest && FromUtf8(state.latest->tag).native()==action,"The requested update is unavailable");
      InstallProgram(root,*state.latest);
      DeleteFileW(ArchivePath(root,*state.latest).c_str());
    }
  }
  (void)Spawn(LocalPath(root,kLauncher),L"--root "+QuoteArgument(root.native()),root);
  return 0;
}
}
