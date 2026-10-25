#pragma once
#include "common/ini.h"
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

namespace cave::launcher::update {
inline constexpr std::string_view kApi="https://api.github.com/repos/zxl0714/EVAC-PC-LABEL/releases/latest";
inline constexpr std::string_view kReleasePrefix="https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/";
using Version=std::array<uint32_t,4>;
std::optional<Version> ParseVersion(std::string_view value);
struct Release {std::string tag,url,notes;};
bool Trusted(const Release& release);
bool Newer(std::string_view local,const Release& release);
std::optional<Release> ParseRelease(std::string_view json);
struct State {
  int64_t checked{},attempted{};
  std::optional<Release> latest;
  std::set<std::string> notified;
};
bool Enabled(const Ini& ini);
int64_t Now();
bool Due(const State& state,int64_t now);
bool ShouldNotify(const State& state,std::string_view local);
State Load(const fs::path& root) noexcept;
void Save(const fs::path& root,const State& state);
struct Result {bool okay{};std::optional<Release> release;std::string error;};
Result Fetch();
void Apply(State& state,const Result& result,int64_t now);
// The worker owns only its result storage. Destruction neither waits for the
// network nor leaves a worker referencing a window, App or collection path.
class Request {
 public:
  explicit Request(std::function<Result()> fetch=Fetch);
  std::optional<Result> Poll();
 private:
  struct Shared {std::mutex mutex;std::optional<Result> result;};
  std::shared_ptr<Shared> shared_;
};
}
