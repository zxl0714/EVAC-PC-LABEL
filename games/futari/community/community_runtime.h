#pragma once

#include "community_client.h"
#include <rex/system/xam/community_service.h>

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

class FutariCommunity final : public rex::system::xam::CommunityService {
 public:
  FutariCommunity(std::string endpoint,std::string nickname,std::string build,
                  std::filesystem::path user_data);
  ~FutariCommunity() override;
  void Read(rex::system::xam::CommunityRead request,ReadCompletion completion) override;
  void Write(std::vector<rex::system::xam::CommunityWrite> views,WriteCompletion completion) override;
  void Replay(rex::system::xam::CommunityReplay request,ReplayCompletion completion) override;
  void Estimate(std::vector<rex::system::xam::CommunityRank> request,RankCompletion completion) override;
  void Stop() override;
  std::string Nickname() const { return nickname_; }
  std::string DisplayName(uint64_t xuid) const override;

 private:
  using Clock=std::chrono::steady_clock;
  struct Task {
    Clock::time_point deadline;
    std::function<void(Clock::time_point)> work;
    std::function<void()> cancel;
  };
  void Enqueue(Task task);
  void Run();
  FutariHttpResult Http(std::string path,nlohmann::json body,Clock::time_point deadline);
  rex::system::xam::CommunityResult ReadNow(const rex::system::xam::CommunityRead& request,Clock::time_point deadline);
  uint32_t WriteNow(const std::vector<rex::system::xam::CommunityWrite>& views,Clock::time_point deadline);
  uint32_t SendBatch(nlohmann::json& batch,const std::filesystem::path& path,Clock::time_point deadline);
  rex::system::xam::CommunityReplayResult ReplayNow(const rex::system::xam::CommunityReplay& request,Clock::time_point deadline);
  uint32_t SendReplay(const nlohmann::json& body,const std::filesystem::path& path,Clock::time_point deadline);
  void RetryOne();
  void ArchiveUnboundReplays();
  std::string NativeName(uint64_t xuid);
  uint64_t NativeToken(const std::string& name);
  FutariLiveClient client_;
  const std::string nickname_;
  const std::string build_;
  const std::filesystem::path user_data_;
  std::mutex mutex_;
  std::mutex stop_mutex_;
  std::condition_variable wake_;
  std::deque<Task> tasks_;
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  std::map<uint64_t,std::string> native_names_;
  mutable std::mutex names_mutex_;
  uint64_t next_token_=0xB000000000000001ull;
  std::map<uint32_t,nlohmann::json> accepted_scores_;
  struct StagedReplay { std::string reference; uint32_t size; };
  std::map<uint32_t,StagedReplay> staged_replays_;
  struct ReplayReference { std::string score, replay, build; };
  std::map<std::pair<uint32_t,uint64_t>,ReplayReference> replay_references_;
};

std::string FutariNativeNickname(std::string name);
std::string FutariCommunityBuildIdentity();
