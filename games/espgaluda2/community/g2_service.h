#pragma once
#include "community/native_service.h"
namespace espgaluda2 {
inline constexpr uint64_t kCommunityLocalXuid=cave::community::kNativeCommunityLocalXuid;
nlohmann::json EncodeCommunityScore(const rex::system::xam::CommunityWrite& view);
std::vector<rex::system::xam::CommunityColumn> DecodeCommunityColumns(const nlohmann::json& properties,uint32_t replay_bytes=0);
std::string CommunityNativeName(std::string_view nickname);
class G2Community final : public cave::community::NativeService {
 public:
  G2Community(std::string endpoint,std::string nickname,std::string build);
};
}
