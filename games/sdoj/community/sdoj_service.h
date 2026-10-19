#pragma once
#include "community/native_service.h"

namespace sdoj {
nlohmann::json EncodeCommunityScore(const rex::system::xam::CommunityWrite&);
std::vector<rex::system::xam::CommunityColumn> DecodeCommunityColumns(const nlohmann::json&,uint32_t replay_bytes=0);
class SdojCommunity final : public cave::community::NativeService {
 public:
  SdojCommunity(std::string endpoint,std::string nickname,std::string build);
};
}
