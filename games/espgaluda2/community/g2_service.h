#pragma once
#include "community/http_client.h"
#include <rex/system/xam/community_service.h>
#include <functional>
#include <chrono>
#include <map>

namespace espgaluda2 {
inline constexpr uint64_t kCommunityLocalXuid = 0xB13EBABEBABEBABEull;
// Pure title schema helpers. Upload properties and read-view columns are distinct.
nlohmann::json EncodeCommunityScore(const rex::system::xam::CommunityWrite& view);
std::vector<rex::system::xam::CommunityColumn> DecodeCommunityColumns(const nlohmann::json& properties, uint32_t replay_bytes = 0);
std::string CommunityNativeName(std::string_view nickname);

class G2Community final : public rex::system::xam::CommunityService {
 public:
  G2Community(std::string endpoint, std::string nickname, std::string build);
  ~G2Community() override;
  void Read(rex::system::xam::CommunityRead request, ReadCompletion completion) override;
  void Write(std::vector<rex::system::xam::CommunityWrite> views, WriteCompletion completion) override;
  void Estimate(std::vector<rex::system::xam::CommunityRank> request, RankCompletion completion) override;
  void Replay(rex::system::xam::CommunityReplay request, ReplayCompletion completion) override;
  void Stop() override;
  std::string DisplayName(uint64_t xuid) const override;
  std::string_view ReplayNamespace() const override { return "espgaluda2-replay"; }
 private:
  using Clock = std::chrono::steady_clock;
  struct Task {
    Clock::time_point deadline;
    std::function<void(Clock::time_point)> work;
    std::function<void(uint32_t)> fail;
  };
  void Enqueue(Task task);
  void Run();
  cave::community::HttpResult Http(std::string path, nlohmann::json body, Clock::time_point deadline);
  rex::system::xam::CommunityResult ReadNow(const rex::system::xam::CommunityRead&, Clock::time_point);
  uint32_t WriteNow(const std::vector<rex::system::xam::CommunityWrite>&, Clock::time_point);
  rex::system::xam::CommunityRankResult EstimateNow(const std::vector<rex::system::xam::CommunityRank>&, Clock::time_point);
  rex::system::xam::CommunityReplayResult ReplayNow(const rex::system::xam::CommunityReplay&, Clock::time_point);
  struct ScoreReference {
    std::string id, replay, hash;
    uint32_t bytes = 0;
    nlohmann::json properties;
  };
  // Accessed only by the service worker. Writes are score-before-replay and may
  // span multiple native batches; references are grouped by the run metadata.
  std::string upload_run_;
  std::map<uint32_t, ScoreReference> uploads_;
  std::map<std::pair<uint32_t,uint64_t>, ScoreReference> downloads_;
  uint64_t NativeToken(const std::string& nickname);
  // Validate before constructing either worker; disabled hosts do not construct this object.
  const std::string nickname_, build_;
  cave::community::HttpClient client_;
  std::mutex mutex_, stop_mutex_;
  mutable std::mutex names_mutex_;
  std::condition_variable wake_;
  std::deque<Task> tasks_;
  std::map<uint64_t, std::string> native_names_;
  uint64_t next_token_ = 0xB000000000000001ull;
  std::atomic<bool> stopping_{false};
  std::thread worker_;
};
}  // namespace espgaluda2
