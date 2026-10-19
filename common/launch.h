#pragma once
#include "common/paths.h"
#include "common/ini.h"
#include <memory>
#include <optional>

namespace cave {
std::wstring QuoteArgument(std::wstring_view argument);
struct LaunchSpec {
  std::string game;
  GamePaths paths;
  fs::path executable,configuration;
  std::vector<std::wstring> arguments;
};
LaunchSpec PrepareLaunch(const Game& game,const fs::path& root,const Ini& ini,
                         std::string_view profile="default",const fs::path& data_override={});
void SetDataDirectory(const fs::path& root,const Game& game,const fs::path& data);

class GameSession final {
 public:
  explicit GameSession(const fs::path& root);
  ~GameSession();
  GameSession(const GameSession&)=delete;
  GameSession& operator=(const GameSession&)=delete;
 private:
  void* handle_{};
};
bool GameIsRunning(const fs::path& root);

class ChildProcess final {
 public:
  ChildProcess()=default;
  ChildProcess(void* handle,unsigned long pid):handle_(handle),pid_(pid){}
  ~ChildProcess();
  ChildProcess(ChildProcess&& other) noexcept;
  ChildProcess& operator=(ChildProcess&& other) noexcept;
  ChildProcess(const ChildProcess&)=delete;
  ChildProcess& operator=(const ChildProcess&)=delete;
  std::optional<unsigned long> ExitCode() const;
  bool RequestedWindowClose(const fs::path& root,std::string_view game,unsigned long code) const;
  unsigned long pid() const {return pid_;}
  void Wait() const;
 private:
  void* handle_{};
  unsigned long pid_{};
};
ChildProcess StartGame(const LaunchSpec& spec);
}  // namespace cave
