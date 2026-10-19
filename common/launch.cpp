#include "common/launch.h"
#include "common/settings.h"
#include "common/config_update.h"
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
  if (game.backend!="xbox360" && game.backend!="ps4") throw std::runtime_error("The selected runtime backend is not integrated yet");
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
  const auto base=fs::canonical(root);
  const auto value=Utf8(Within(data,base)?data.lexically_relative(base):data);
  if (value.find_first_of("\r\n")!=value.npos) throw std::runtime_error("Invalid INI directory value");
  const auto section="game."+std::string(game.id);
  const std::array edits{IniEdit{section,"data_dir",value}};
  UpdateConfig(root,before,edits);
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
bool ChildProcess::RequestedWindowClose(const fs::path& root,std::string_view game,unsigned long code) const {
  // 27 is not generally a successful exit. Only accept this backend's explicit
  // presentation-cleanup receipt, bound to this process's PID AND birth time.
  if (!handle_ || (game!="ketsui" && game!="daioujou") || code!=27) return false;
  try {
    FILETIME created{},exited{},kernel{},user{};
    if (!GetProcessTimes(handle_,&created,&exited,&kernel,&user)) return false;
    const auto birth=std::to_string((std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime);
    const auto logs=DataChild(root,"logs/"+std::string(game));
    if (!fs::is_directory(logs)) return false;
    for (const auto& item:fs::directory_iterator(logs)) {
      const auto name=Utf8(item.path().filename());
      if (!name.starts_with("run-") || !name.ends_with("Z-"+std::to_string(pid_))) continue;
      const auto receipt=DataChild(logs,name+"/exit.ini");
      if (!fs::is_regular_file(receipt)) continue;
      const auto ini=Ini::Load(receipt);
      auto matches=[&](std::string_view key,std::string_view value) {const auto entry=ini.Get("exit",key);return entry && entry->text==value;};
      if (matches("game",game) && matches("pid",std::to_string(pid_)) && matches("process_created",birth) &&
          matches("code","27") && matches("reason","window_close_requested") && matches("presentation_cleanup_complete","true") &&
          matches("guest_finalizers_run","false") && matches("host_cleanup_complete","false")) return true;
    }
  } catch (...) { /* Missing/invalid evidence remains an abnormal exit. */ }
  return false;
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
