#include "common/launch.h"
#include "common/controller_bindings.h"
#include "common/verification_cache.h"
#include <chrono>
#include <cstdio>
#include "common/settings.h"
#include "common/config_update.h"
#include "common/stfs.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace cave {
namespace {
[[noreturn]] void WinError(const char* operation) {
  throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");
}
std::wstring SessionName(const fs::path& requested) {
  // Preserve this existing game-session namespace across the product rename.
  return L"Local\\CavePCCollection.Game.v1."+CollectionIdentity(requested);
}
std::wstring GameEnvironment(std::string_view controller_mapping_hint) {
  if(controller_mapping_hint.empty())return {};
  struct Environment {
    wchar_t* data=GetEnvironmentStringsW();
    ~Environment(){if(data)FreeEnvironmentStringsW(data);}
  } environment;
  if(!environment.data)WinError("Read launch environment");
  constexpr std::wstring_view name=L"SDL_GAMECONTROLLERCONFIG=";
  std::wstring mapping(name);
  std::vector<std::wstring_view> entries;
  for(auto* next=environment.data;*next;) {
    const std::wstring_view entry(next);
    if(entry.size()>=name.size() && CompareStringOrdinal(entry.data(),int(name.size()),name.data(),int(name.size()),TRUE)==CSTR_EQUAL) {
      if(entry.size()>name.size()) {mapping.append(entry.substr(name.size()));mapping+=L'\n';}
    } else entries.push_back(entry);
    next+=entry.size()+1;
  }
  mapping+=FromUtf8(controller_mapping_hint).native();
  entries.push_back(mapping);
  std::sort(entries.begin(),entries.end(),[](auto a,auto b) {
    return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_LESS_THAN;
  });
  std::wstring block;
  for(auto entry:entries){block.append(entry);block+=L'\0';}
  block+=L'\0';
  return block;
}
void VerifyRuntime(const Game& game,const fs::path& root,VerificationCache& cache) {
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
    if (!entry || entry->text.size()!=64 || cache.Digest(DataChild(root,name))!=entry->text)
      throw std::runtime_error("Runtime component differs from its verified build: "+std::string(name));
  }
  if (actual_count!=expected_count) throw std::runtime_error("Runtime manifest has unexpected components");
}
bool PatchMatches(const fs::path& path, const XexTitlePatch& patch, VerificationCache* cache) {
  if (!fs::is_regular_file(path)) return false;
  const auto hash = patch.HashForSize(fs::file_size(path));
  return !hash.empty() && FileDigest(path, cache) == hash;
}
void InstallTitleUpdate(const Game& game, const fs::path& data, VerificationCache& cache) {
  const auto& update = *game.title_update;
  if (std::ranges::all_of(update.patches, [&](const auto& patch) {
      return fs::exists(DataChild(data, patch.file.path));
    })) return;
  const auto source = DataChild(data, update.container.path);
  if (!fs::exists(source)) return;

  for (const auto& file : game.files) {
    if (!file.program) continue;
    const auto path = DataChild(data, file.path);
    if (!fs::is_regular_file(path) || fs::file_size(path) != file.bytes || cache.Digest(path) != file.sha256)
      return;
  }
  if (!PatchMatches(source, XexTitlePatch{update.container}, &cache))
    throw std::runtime_error("Unsupported title update: " + std::string(update.container.path));

  std::vector<const XexTitlePatch*> missing;
  for (const auto& patch : update.patches) {
    const auto path = DataChild(data, patch.file.path);
    if (!fs::exists(path)) missing.push_back(&patch);
    else if (!PatchMatches(path, patch, &cache))
      throw std::runtime_error("Existing title update file differs from the supported update: " + std::string(patch.file.path));
  }
  if (missing.empty()) return;

  StfsPackage archive(source, true, kStfsTitleUpdate);
  if (archive.TitleId() != update.title_id || archive.Files().size() != update.patches.size() ||
      !std::ranges::all_of(update.patches, [&](const auto& patch) {
        return std::ranges::any_of(archive.Files(), [&](const auto& member) {
          return member.path == patch.file.path && member.bytes == patch.file.bytes &&
                 member.sha256 == patch.file.sha256;
        });
      })) throw std::runtime_error("Title update contents differ from the supported update");

  static std::atomic_uint64_t sequence{};
  const auto staging = DataChild(data, ".tu-" + std::to_string(GetCurrentProcessId()) + "-" +
      std::to_string(GetTickCount64()) + "-" + std::to_string(sequence++));
  if (!fs::create_directory(staging)) throw std::runtime_error("Cannot create title update staging directory");
  struct Cleanup {
    const fs::path& staging;
    ~Cleanup() {
      std::error_code error;
      if (fs::canonical(staging, error) == staging) fs::remove_all(staging, error);
    }
  } cleanup{staging};
  const auto payload = staging / "payload";
  archive.Extract(payload);
  for (const auto* patch : missing) {
    const auto path = DataChild(payload, patch->file.path);
    if (patch->padded_bytes > patch->file.bytes) {
      // Match the disc patch layout used by SDOJ-Recomp when preparing the shared data.
      const std::string padding(patch->padded_bytes - patch->file.bytes, '\0');
      std::ofstream output(path, std::ios::binary | std::ios::app);
      output.write(padding.data(), std::streamsize(padding.size()));
      output.close();
      if (!output) throw std::runtime_error("Cannot write title update padding");
    }
    if (!PatchMatches(path, *patch, nullptr)) throw std::runtime_error("Extracted title update failed verification");
  }
  for (const auto* patch : missing) {
    const auto target = data / FromUtf8(patch->file.path);
    if (!MoveFileExW(DataChild(payload, patch->file.path).c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
      const auto error = GetLastError();
      if (!PatchMatches(DataChild(data, patch->file.path), *patch, &cache))
        throw std::runtime_error("Cannot install title update file " + std::string(patch->file.path) +
            " (Windows error " + std::to_string(error) + ")");
    }
  }
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
Inspection InspectLaunchData(const Game& game, const fs::path& root, const fs::path& data,
                             CheckLevel level, VerificationCache& cache) {
  if (game.title_update) {
    try {
      const auto paths = Paths(root, game.id, data);
      ValidateOutputs(paths);
      InstallTitleUpdate(game, paths.data, cache);
    } catch (const std::exception& error) {
      auto inspection = Inspect(game, root, data, CheckLevel::Quick, &cache);
      if (inspection.data_state == DataState::Invalid) return inspection;
      inspection.state = "title_update_failed";
      inspection.data_state = DataState::Incomplete;
      inspection.reason = error.what();
      return inspection;
    }
  }
  return Inspect(game, root, data, level, &cache, TitleUpdateInput::PatchesOnly);
}
LaunchSpec PrepareLaunch(const Game& game,const fs::path& root,const Ini& ini,std::string_view profile,const fs::path& override_data) {
  if (game.coming_soon || !game.integrated) throw std::runtime_error("This game is not integrated yet");
  if (game.backend!="xbox360" && game.backend!="ps4" && game.backend!="ps2") throw std::runtime_error("The selected runtime backend is not integrated yet");
  (void)ResolveSettings(ini,game.id);
  LaunchSpec result;
  result.game=game.id;
  result.controller_mapping_hint=ControllerMappingHint(ini,game.id);
  result.paths=Paths(root,game.id,override_data.empty()?ini.DataDirectory(game):override_data,profile);
  ValidateOutputs(result.paths);
  VerificationCache cache(result.paths.root);
  const auto started=std::chrono::steady_clock::now();
  const auto inspection=InspectLaunchData(game,result.paths.root,result.paths.data,CheckLevel::Full,cache);
  const auto data_done=std::chrono::steady_clock::now();
  if (inspection.state!="ready") {
    auto reason=inspection.reason;
    if (!inspection.problems.empty()) reason+=" / "+inspection.problems.front();
    if (!inspection.missing_runtime.empty()) reason+=" / "+inspection.missing_runtime.front();
    throw std::runtime_error(reason);
  }
  VerifyRuntime(game,result.paths.root,cache);
  const auto runtime_done=std::chrono::steady_clock::now();
  char measurement[320]{};
  std::snprintf(measurement,sizeof(measurement),"game=%.*s data_ms=%.3f runtime_ms=%.3f cache_hits=%llu reads=%llu bytes=%llu",
      int(game.id.size()),game.id.data(),std::chrono::duration<double,std::milli>(data_done-started).count(),
      std::chrono::duration<double,std::milli>(runtime_done-data_done).count(),cache.hits,cache.reads,cache.bytes_read);
  result.verification=measurement;
  result.configuration=ConfigPath(result.paths.root);
  const auto executable = game.title_update && inspection.variant == game.title_update->id
      ? game.title_update->executable : game.executable;
  result.executable=DataChild(result.paths.root,executable);
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
uint64_t ChildProcess::CreationTime() const noexcept {
  FILETIME created{},exited{},kernel{},user{};
  return handle_ && GetProcessTimes(handle_,&created,&exited,&kernel,&user)
      ? (uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime : 0;
}
bool ChildProcess::RequestedWindowClose(const fs::path& root,std::string_view game,unsigned long code) const {
  // 27 is not generally a successful exit. Only accept this backend's explicit
  // presentation-cleanup receipt, bound to this process's PID AND birth time.
  if (!handle_ || (game!="ketsui" && game!="daioujou" && game!="esprade" && game!="feveron" && game!="garegga") || code!=27) return false;
  try {
    const auto created=CreationTime();
    if (!created) return false;
    const auto birth=std::to_string(created);
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
ChildProcess StartGame(const LaunchSpec& spec,const fs::path& working_directory) {
  if (GameIsRunning(spec.paths.root)) throw std::runtime_error("A game is already running from this collection");
  std::wstring command=QuoteArgument(spec.executable.native());
  for (const auto& argument:spec.arguments) command+=L" "+QuoteArgument(argument);
  if (command.size()>=32767) throw std::runtime_error("Game command line is too long");
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  auto environment=GameEnvironment(spec.controller_mapping_hint);
  const auto& directory=working_directory.empty()?spec.paths.root:working_directory;
  if (!CreateProcessW(spec.executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
                      environment.empty()?nullptr:environment.data(),directory.c_str(),&startup,&process)) WinError("Start selected game");
  CloseHandle(process.hThread);
  return ChildProcess(process.hProcess,process.dwProcessId);
}
}  // namespace cave
