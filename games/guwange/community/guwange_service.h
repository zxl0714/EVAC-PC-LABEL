#pragma once
#include "community/native_service.h"

namespace guwange {
bool ValidView(uint32_t view);
nlohmann::json EncodeCommunityScore(const rex::system::xam::CommunityWrite&);
std::vector<rex::system::xam::CommunityColumn> DecodeCommunityColumns(
    uint32_t view,const nlohmann::json& properties,uint32_t replay_bytes=0);
void MatchCommunityReplay(uint32_t view,const nlohmann::json& properties,std::span<const uint8_t> record);
class GuwangeCommunity final : public cave::community::NativeService {
 public:
  GuwangeCommunity(std::string endpoint,std::string nickname,std::string build);
  void Read(rex::system::xam::CommunityRead request,ReadCompletion completion) override;
};
}
