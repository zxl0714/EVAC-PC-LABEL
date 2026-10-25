#include "update_check.h"
#include "common/paths.h"
#include "common/config_update.h"
#include "common/settings.h"
#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <fstream>
#include <thread>

namespace cave::launcher::update {
namespace {
using Json=nlohmann::json;
constexpr size_t kResponseLimit=128*1024,kCacheLimit=256*1024,kNotesLimit=12000;
std::string Notes(std::string value) {
  if(value.size()>kNotesLimit) {
    size_t end=kNotesLimit;
    while(end && (static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
    value.resize(end);value+="\n…";
  }
  for(auto& c:value)if(static_cast<unsigned char>(c)<32 && c!='\n' && c!='\t' && c!='\r')c=' ';
  // Validate text before it reaches the native dialog.
  (void)FromUtf8(value);return value;
}
fs::path CacheFile(const fs::path& root) {
  const auto base=fs::canonical(root),path=base/"user-data/update-check.json";
  for(auto current=path;current!=base;current=current.parent_path()) {
    const auto flags=GetFileAttributesW(current.c_str());
    if(flags!=INVALID_FILE_ATTRIBUTES && (flags&FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Update cache cannot use linked paths");
  }
  const auto ini=Ini::Load(ConfigPath(base));
  for(const auto& game:Catalogue()) {
    const auto data=Resolve(base,ini.DataDirectory(game));
    const auto content=ini.ContentDirectory(game,data);
    for(const auto& input:std::array{data,content.empty()?fs::path{}:Resolve(base,content)})
      if(!input.empty() && (Within(path.parent_path(),input) || Within(input,path.parent_path())))
        throw std::runtime_error("Update cache overlaps game data");
  }
  return path;
}
struct Internet {
  HINTERNET value{};
  explicit Internet(HINTERNET handle):value(handle) {if(!value)throw std::runtime_error("GitHub connection failed");}
  ~Internet(){WinHttpCloseHandle(value);}
  Internet(const Internet&)=delete;
};
void Check(BOOL result){if(!result)throw std::runtime_error("GitHub request failed ("+std::to_string(GetLastError())+")");}
}
std::optional<Version> ParseVersion(std::string_view value) {
  if(value.starts_with('v'))value.remove_prefix(1);
  if(value.empty() || value.size()>32)return {};
  Version result{};unsigned count=0;
  while(!value.empty()) {
    if(count==result.size())return {};
    const auto dot=value.find('.');const auto piece=value.substr(0,dot);
    if(piece.empty() || piece.find_first_not_of("0123456789")!=piece.npos)return {};
    const auto [end,error]=std::from_chars(piece.data(),piece.data()+piece.size(),result[count++]);
    if(error!=std::errc{} || end!=piece.data()+piece.size())return {};
    if(dot==value.npos)break;
    value.remove_prefix(dot+1);if(value.empty())return {};
  }
  return count>=2?std::optional(result):std::nullopt;
}
bool Trusted(const Release& release) {
  return ParseVersion(release.tag).has_value() && release.url==std::string(kReleasePrefix)+release.tag;
}
bool Newer(std::string_view local,const Release& release) {
  const auto current=ParseVersion(local),remote=ParseVersion(release.tag);
  return current && remote && Trusted(release) && *remote>*current;
}
std::optional<Release> ParseRelease(std::string_view body) {
  if(body.empty() || body.size()>kResponseLimit)throw std::runtime_error("Invalid GitHub response size");
  const auto json=Json::parse(body);
  if(json.at("draft").get<bool>() || json.at("prerelease").get<bool>())return {};
  Release release{json.at("tag_name").get<std::string>(),json.at("html_url").get<std::string>(),{}};
  if(!Trusted(release))throw std::runtime_error("Unexpected release version or page");
  if(json.contains("body") && !json.at("body").is_null())release.notes=Notes(json.at("body").get<std::string>());
  return release;
}
bool Enabled(const Ini& ini) {
  const auto value=ini.Get("collection","check_for_updates");
  if(!value)return true;
  if(value->text=="true" || value->text=="1")return true;
  if(value->text=="false" || value->text=="0")return false;
  throw std::runtime_error("[collection] check_for_updates: expected true or false");
}
int64_t Now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool Due(const State& state,int64_t now) {
  const auto recent=[&](int64_t at,int64_t ttl){return at>0 && at<=now+300 && now-at<ttl;};
  return !recent(state.checked,24*60*60) && !recent(state.attempted,60*60);
}
bool ShouldNotify(const State& state,std::string_view local) {
  return state.latest && Newer(local,*state.latest) && !state.notified.contains(state.latest->tag);
}
State Load(const fs::path& root) noexcept {
  try {
    const auto path=CacheFile(root);
    if(!fs::is_regular_file(path) || fs::file_size(path)>kCacheLimit)return {};
    std::ifstream stream(path,std::ios::binary);
    std::string bytes(kCacheLimit+1,'\0');stream.read(bytes.data(),std::streamsize(bytes.size()));
    if(stream.bad() || stream.gcount()>std::streamsize(kCacheLimit))return {};
    bytes.resize(size_t(stream.gcount()));
    const auto json=Json::parse(bytes);
    if(json.at("schema").get<int>()!=1)return {};
    State result;result.checked=json.at("checked").get<int64_t>();result.attempted=json.at("attempted").get<int64_t>();
    if(!json.at("latest").is_null()) {
      const auto& item=json.at("latest");
      Release release{item.at("tag").get<std::string>(),item.at("url").get<std::string>(),Notes(item.at("notes").get<std::string>())};
      if(!Trusted(release))return {};
      result.latest=std::move(release);
    }
    const auto& notified=json.at("notified");if(!notified.is_array() || notified.size()>1024)return {};
    for(const auto& entry:notified) {const auto tag=entry.get<std::string>();if(!ParseVersion(tag))return {};result.notified.insert(tag);}
    return result;
  }catch(...){return {};}
}
void Save(const fs::path& root,const State& state) {
  Json json{{"schema",1},{"checked",state.checked},{"attempted",state.attempted},{"notified",state.notified},{"latest",nullptr}};
  if(state.latest) {
    if(!Trusted(*state.latest))throw std::runtime_error("Untrusted update cache page");
    json["latest"]={{"tag",state.latest->tag},{"url",state.latest->url},{"notes",Notes(state.latest->notes)}};
  }
  const auto bytes=json.dump(2);if(bytes.size()>kCacheLimit || state.notified.size()>1024)throw std::runtime_error("Update cache too large");
  const auto path=CacheFile(root);fs::create_directories(path.parent_path());
  const auto pending=fs::path(path.native()+L".pending-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  const auto file=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot write update cache");
  DWORD written{};const bool okay=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if(!okay || !MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(pending.c_str());throw std::runtime_error("Cannot save update cache");
  }
}
Result Fetch() {
  try {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    Internet session(WinHttpOpen(L"EVAC-PC-LABEL-update/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    Check(WinHttpSetTimeouts(session.value,2000,2000,3000,3000));
    Internet connection(WinHttpConnect(session.value,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    Internet request(WinHttpOpenRequest(connection.value,L"GET",L"/repos/zxl0714/EVAC-PC-LABEL/releases/latest",nullptr,
      WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_REDIRECTS;
    Check(WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)));
    constexpr wchar_t headers[]=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    Check(WinHttpSendRequest(request.value,headers,DWORD(-1),WINHTTP_NO_REQUEST_DATA,0,0,0));
    Check(WinHttpReceiveResponse(request.value,nullptr));
    DWORD status{},size=sizeof(status);Check(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if(status!=200)throw std::runtime_error("GitHub HTTP "+std::to_string(status));
    std::string body;std::array<char,8192> buffer{};
    for(;;) {
      if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("GitHub request timed out");
      DWORD read{};Check(WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&read));
      if(!read)break;
      if(body.size()+read>kResponseLimit)throw std::runtime_error("GitHub response too large");
      body.append(buffer.data(),read);
    }
    return {true,ParseRelease(body),{}};
  }catch(const std::exception& error){return {false,{},error.what()};}
}
void Apply(State& state,const Result& result,int64_t now) {
  state.attempted=now;
  if(result.okay) {state.checked=now;state.latest=result.release;}
}
Request::Request(std::function<Result()> fetch):shared_(std::make_shared<Shared>()) {
  std::thread([shared=shared_,fetch=std::move(fetch)] {
    Result result;
    try {result=fetch();}catch(const std::exception& error){result.error=error.what();}catch(...){result.error="Update request failed";}
    std::lock_guard lock(shared->mutex);shared->result=std::move(result);
  }).detach();
}
std::optional<Result> Request::Poll() {
  std::lock_guard lock(shared_->mutex);auto result=std::move(shared_->result);shared_->result.reset();return result;
}
}
