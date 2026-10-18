#include "common/launch.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace cave {
namespace {
[[noreturn]] void WinError(const char* operation) {
  throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");
}
std::wstring SessionName(const fs::path& requested) {
  const auto root=fs::canonical(requested);
  HANDLE directory=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                              nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if (directory==INVALID_HANDLE_VALUE) WinError("Open collection directory");
  std::wstring path(32768,L'\0');
  const auto count=GetFinalPathNameByHandleW(directory,path.data(),DWORD(path.size()),FILE_NAME_NORMALIZED);
  CloseHandle(directory);
  if (!count || count>=path.size()) throw std::runtime_error("Cannot resolve collection identity");
  path.resize(count);
  const auto upper_count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),int(path.size()),nullptr,0,nullptr,nullptr,0);
  if (!upper_count) WinError("Normalize collection identity");
  std::wstring upper(upper_count,L'\0');
  if (!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),int(path.size()),upper.data(),upper_count,nullptr,nullptr,0))
    WinError("Normalize collection identity");
  std::array<unsigned char,32> hash{};
  BCRYPT_ALG_HANDLE algorithm{};
  if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
    throw std::runtime_error("Cannot hash collection identity");
  const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(upper.data()),ULONG(upper.size()*sizeof(wchar_t)),hash.data(),ULONG(hash.size()));
  BCryptCloseAlgorithmProvider(algorithm,0);
  if (status<0) throw std::runtime_error("Cannot hash collection identity");
  constexpr wchar_t digits[]=L"0123456789abcdef";
  std::wstring name=L"Local\\CavePCCollection.Game.v1.";
  for (auto byte:hash) {name+=digits[byte>>4];name+=digits[byte&15];}
  return name;
}
std::string Lower(std::string text) {
  for (auto& c:text) if (c>='A' && c<='Z') c+=32;
  return text;
}
std::string_view Trim(std::string_view text) {
  const auto first=text.find_first_not_of(" \t\r");
  return first==text.npos?std::string_view{}:text.substr(first,text.find_last_not_of(" \t\r")-first+1);
}
void VerifyRuntime(const Game& game,const fs::path& root) {
  const auto certificate="runtime/games/"+std::string(game.id)+"/runtime-manifest.ini";
  const auto ini=Ini::Load(DataChild(root,certificate));
  const auto identity=ini.Get("runtime","game"), contract=ini.Get("runtime","contract");
  if (!identity || identity->text!=game.id || !contract || contract->text!="1")
    throw std::runtime_error("Missing or incompatible runtime manifest; rebuild the selected game");
  size_t expected_count=0,actual_count=0;
  for (const auto& [address,value]:ini.Values()) if (address.first=="files") ++actual_count;
  for (const auto name:game.runtime_files) {
    if (name==certificate) continue;
    ++expected_count;
    const auto entry=ini.Get("files",name);
    if (!entry || entry->text.size()!=64 || Sha256(DataChild(root,name))!=entry->text)
      throw std::runtime_error("Runtime component differs from its verified build: "+std::string(name));
  }
  if (actual_count!=expected_count) throw std::runtime_error("Runtime manifest has unexpected components");
}
}

std::wstring QuoteArgument(std::wstring_view argument) {
  std::wstring result=L"\"";
  size_t slashes=0;
  for (const auto character:argument) {
    if (character==L'\\') {++slashes;continue;}
    if (character==L'\"') {result.append(slashes*2+1,L'\\');result+=character;}
    else {result.append(slashes,L'\\');result+=character;}
    slashes=0;
  }
  result.append(slashes*2,L'\\');
  result+=L'\"';
  return result;
}
LaunchSpec PrepareLaunch(const Game& game,const fs::path& root,const Ini& ini,std::string_view profile,const fs::path& override_data) {
  if (game.coming_soon || !game.integrated) throw std::runtime_error("This game is not integrated yet");
  if (game.backend!="xbox360") throw std::runtime_error("The selected runtime backend is not integrated yet");
  (void)ResolveSettings(ini,game.id);
  LaunchSpec result;
  result.game=game.id;
  result.paths=Paths(root,game.id,override_data.empty()?ini.DataDirectory(game):override_data,profile);
  ValidateOutputs(result.paths);
  const auto inspection=Inspect(game,result.paths.root,result.paths.data,CheckLevel::Full);
  if (inspection.state!="ready") {
    auto reason=inspection.reason;
    if (!inspection.problems.empty()) reason+=" / "+inspection.problems.front();
    if (!inspection.missing_runtime.empty()) reason+=" / "+inspection.missing_runtime.front();
    throw std::runtime_error(reason);
  }
  VerifyRuntime(game,result.paths.root);
  result.configuration=result.paths.root/"config.ini";
  result.executable=DataChild(result.paths.root,game.executable);
  result.arguments={L"--collection_root="+result.paths.root.native(),L"--game_id="+FromUtf8(game.id).native(),L"--launch_contract=1",
                    L"--game_data_root="+result.paths.data.native(),L"--user_data_root="+result.paths.profile.native()};
  return result;
}
void SetDataDirectory(const fs::path& root,const Game& game,const fs::path& requested) {
  const auto data=fs::canonical(requested);
  if (!fs::is_directory(data)) throw std::runtime_error("Select a game-data directory");
  ValidateOutputs(Paths(root,game.id,data));
  const auto config=EnsureConfig(root);
  const auto before=Ini::ReadText(config);
  const auto ini=Ini::Parse(before);
  const auto value=Utf8(data);
  if (value.find_first_of("\r\n")!=value.npos) throw std::runtime_error("Invalid INI directory value");
  const auto section="game."+std::string(game.id);
  std::string after=before;
  const auto old=ini.Get(section,"data_dir");
  const auto newline=before.find("\r\n")!=before.npos?"\r\n":"\n";
  if (old) {
    size_t start=0;
    for (size_t number=1;number<old->line;++number) start=before.find('\n',start)+1;
    auto end=before.find('\n',start);
    if (end==before.npos) end=before.size();
    if (end>start && before[end-1]=='\r') --end;
    const auto equal=before.find('=',start);
    start=equal+1;
    while (start<end && (before[start]==' ' || before[start]=='\t')) ++start;
    after.replace(start,end-start,value);
  } else {
    size_t start=0,insert=std::string::npos;
    while (start<before.size()) {
      auto end=before.find('\n',start);if (end==before.npos) end=before.size();
      auto line=Trim(std::string_view(before).substr(start,end-start));
      if (start==0 && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
      if (line.size()>=2 && line.front()=='[' && line.back()==']' && Lower(std::string(Trim(line.substr(1,line.size()-2))))==section)
        {insert=end<before.size()?end+1:end;break;}
      start=end<before.size()?end+1:end;
    }
    const auto row="data_dir = "+value+newline;
    if (insert!=before.npos) after.insert(insert,(insert && before[insert-1]!='\n'?std::string(newline):"")+row);
    else {if (!after.empty() && !after.ends_with('\n')) after+=newline;after+=std::string(newline)+"["+section+"]"+newline+row;}
  }
  (void)Ini::Parse(after);
  std::array<unsigned char,16> nonce{};
  if (BCryptGenRandom(nullptr,nonce.data(),ULONG(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
    throw std::runtime_error("Cannot create configuration transaction");
  std::wstring suffix;
  for (auto byte:nonce) suffix+=std::to_wstring(byte)+L"-";
  const auto temporary=fs::path(config.native()+L".pending-"+suffix);
  HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if (file==INVALID_HANDLE_VALUE) WinError("Create configuration transaction");
  DWORD written{};
  const bool okay=WriteFile(file,after.data(),DWORD(after.size()),&written,nullptr) && written==after.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!okay || Ini::ReadText(config)!=before) {
    DeleteFileW(temporary.c_str());
    throw std::runtime_error("Configuration changed while saving; retry after closing the editor");
  }
  if (!MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    const auto error=GetLastError();DeleteFileW(temporary.c_str());SetLastError(error);WinError("Replace config.ini");
  }
}

GameSession::GameSession(const fs::path& root) {
  const auto name=SessionName(root);
  HANDLE handle=CreateMutexW(nullptr,FALSE,name.c_str());
  if (!handle) WinError("Create game session lock");
  const auto wait=WaitForSingleObject(handle,0);
  if (wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) {
    CloseHandle(handle);
    throw std::runtime_error("A game is already running from this collection");
  }
  handle_=handle;
}
GameSession::~GameSession() {if (handle_) {ReleaseMutex(handle_);CloseHandle(handle_);}}
bool GameIsRunning(const fs::path& root) {
  HANDLE handle=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,SessionName(root).c_str());
  if (!handle) {if (GetLastError()==ERROR_FILE_NOT_FOUND) return false;WinError("Inspect game session lock");}
  const auto wait=WaitForSingleObject(handle,0);
  if (wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) ReleaseMutex(handle);
  CloseHandle(handle);
  if (wait==WAIT_FAILED) WinError("Inspect game session lock");
  return wait==WAIT_TIMEOUT;
}
ChildProcess::~ChildProcess() {if (handle_) CloseHandle(handle_);}
ChildProcess::ChildProcess(ChildProcess&& other) noexcept:handle_(std::exchange(other.handle_,nullptr)),pid_(std::exchange(other.pid_,0)){}
ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
  if (this!=&other) {if(handle_)CloseHandle(handle_);handle_=std::exchange(other.handle_,nullptr);pid_=std::exchange(other.pid_,0);}return *this;
}
std::optional<unsigned long> ChildProcess::ExitCode() const {
  if (!handle_) return 0;
  if (WaitForSingleObject(handle_,0)==WAIT_TIMEOUT) return {};
  DWORD code{};if (!GetExitCodeProcess(handle_,&code)) WinError("Read game exit code");return code;
}
void ChildProcess::Wait() const {if (handle_ && WaitForSingleObject(handle_,INFINITE)==WAIT_FAILED) WinError("Wait for game process");}
ChildProcess StartGame(const LaunchSpec& spec) {
  if (GameIsRunning(spec.paths.root)) throw std::runtime_error("A game is already running from this collection");
  std::wstring command=QuoteArgument(spec.executable.native());
  for (const auto& argument:spec.arguments) command+=L" "+QuoteArgument(argument);
  if (command.size()>=32767) throw std::runtime_error("Game command line is too long");
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if (!CreateProcessW(spec.executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                      nullptr,spec.paths.root.c_str(),&startup,&process)) WinError("Start selected game");
  CloseHandle(process.hThread);
  return ChildProcess(process.hProcess,process.dwProcessId);
}
}  // namespace cave
