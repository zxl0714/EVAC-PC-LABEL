#include "community/guwange_service.h"
#include "community/replay_format.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>

namespace guwange {
using namespace rex::system::xam;
using Json=nlohmann::json;
namespace {
void Check(bool ok){if(!ok)throw std::runtime_error("Invalid Guwange community data");}
constexpr std::pair<uint32_t,uint32_t> kTypes[]{
    {0x20000004,2},{0x10000008,1},{0x10000006,1},{0x10000001,1},
    {0x10000003,1},{0x20000002,2},{0x10000005,1},{0x10008011,1}};
uint32_t Type(uint32_t id) {
  for(const auto& [key,type]:kTypes)if(key==id)return type;
  return 0;
}
int64_t Number(const Json& value,uint32_t type) {
  if(type==1){Check(value.is_number_integer());return value.get<int64_t>();}
  Check(value.is_string());const auto& text=value.get_ref<const std::string&>();
  int64_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
  Check(!text.empty() && text.size()<=20 && parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size());
  return result;
}
std::map<uint32_t,int64_t> Properties(const Json& props) {
  Check(props.is_array() && props.size()>=7 && props.size()<=8);
  std::map<uint32_t,int64_t> result;
  for(const auto& item:props) {
    Check(item.at("id").is_number_integer() && item.at("type").is_number_integer());
    const auto id64=item.at("id").get<int64_t>();Check(id64>=0 && id64<=UINT32_MAX);
    const auto id=uint32_t(id64),type=Type(id);Check(type && item.at("type")==type);
    const auto value=Number(item.at("value"),type);Check(result.emplace(id,value).second);
    if(type==1)Check(value>=INT32_MIN && value<=INT32_MAX);
  }
  for(const auto& [id,type]:kTypes)if(id!=0x10008011)Check(result.contains(id));
  Check(result.at(0x20000004)>=0 && result.at(0x20000004)<=INT32_MAX);
  Check(result.at(0x10000008)>=0 && result.at(0x10000008)<=2);
  Check(result.at(0x10000006)>=INT16_MIN && result.at(0x10000006)<=INT16_MAX);
  for(auto id:{0x10000001,0x10000003,0x10000005})Check(result.at(id)==0);
  Check(result.at(0x20000002)>0);
  if(result.contains(0x10008011)) {
    const auto bytes=result.at(0x10008011);
    Check(!bytes || (bytes>=kReplayHeaderBytes+kReplayFixedPayloadBytes && bytes<=kReplayMaxBytes));
  }
  return result;
}
void CheckView(uint32_t view,const std::map<uint32_t,int64_t>& props) {
  Check(ValidView(view));const auto slot=(view-1)%4;
  if(slot)Check(props.at(0x10000008)==slot-1);
}
std::string RunKey(const Json& props) {
  const auto p=Properties(props);
  return std::to_string(p.at(0x20000004))+"/"+std::to_string(p.at(0x20000002))+"/"+
      std::to_string(p.at(0x10000008))+"/"+std::to_string(p.at(0x10000006));
}
cave::community::NativeSchema Schema() {
  return {.game="guwange",.title="58410A78",.replay_namespace="guwange-replay",
      .replay_format=kReplayFormat,.replay_header=kReplayHeaderBytes,.replay_max=kReplayMaxBytes,
      .valid_view=ValidView,.encode_score=EncodeCommunityScore,.decode_columns=DecodeCommunityColumns,
      .column_type=[](uint16_t){return uint32_t(1);},.native_name=cave::community::NativeLegacyName,
      .run_key=RunKey,.match_replay=MatchCommunityReplay,.storage_size_property=0x10008011};
}
}
bool ValidView(uint32_t view){return view>=1 && view<=12;}
Json EncodeCommunityScore(const CommunityWrite& view) {
  Json columns=Json::array();
  for(const auto& col:view.columns) {
    Check(Type(col.id)==col.type && col.type);
    columns.push_back({{"id",col.id},{"type",col.type},
        {"value",col.type==2?Json(std::to_string(col.integer)):Json(col.integer)}});
  }
  const auto p=Properties(columns);CheckView(view.view,p);
  return {{"view",view.view},{"rules","standard"},{"score",std::to_string(p.at(0x20000004))},
      {"columns",std::move(columns)}};
}
std::vector<CommunityColumn> DecodeCommunityColumns(uint32_t view,const Json& props,uint32_t bytes) {
  const auto p=Properties(props);CheckView(view,p);
  Check(!bytes || (bytes>=kReplayHeaderBytes+kReplayFixedPayloadBytes && bytes<=kReplayMaxBytes));
  // Base reader 821B67E8: view 1 has character/stage columns 4/5;
  // all other views have 6/4. Requested columns 1/2/3/7 are not read here.
  // Do not invent their semantics from the writer property IDs.
  return {{view==1?4u:6u,1,p.at(0x10000008),0},
      {view==1?5u:4u,1,p.at(0x10000006),0},{0xfffa,1,bytes,0}};
}
void MatchCommunityReplay(uint32_t view,const Json& props,std::span<const uint8_t> record) {
  const auto p=Properties(props);CheckView(view,p);const auto info=InspectReplay(record);
  Check(info.mode==(view-1)/4 && info.stage==p.at(0x10000006));
  Check(std::any_of(info.players.begin(),info.players.end(),[&](const auto& player){
    return player.character==p.at(0x10000008) && player.score==p.at(0x20000004);
  }));
  // Score time (821BF1D4) is generated after recording time (821936C8).
  // Preserve both; requiring equality rejects original native recordings.
  if(p.contains(0x10008011))Check(!p.at(0x10008011) || p.at(0x10008011)==int64_t(record.size()));
}
GuwangeCommunity::GuwangeCommunity(std::string endpoint,std::string nickname,std::string build)
    :NativeService(Schema(),std::move(endpoint),std::move(nickname),std::move(build)){}
void GuwangeCommunity::Read(CommunityRead request,ReadCompletion completion) {
  // Base 821D56C0 asks for one row centred on an XUID to compare personal
  // bests. It reads pRows->rating at 821D5968 without checking cRows. Use the
  // explicit-user contract: a missing player has rank/rating 0, not a null
  // pRows or another player's top score from the around-user fallback. This
  // row is local to the reply; it never creates a public leaderboard entry.
  // Multi-row around-user navigation retains its normal paging behaviour.
  if(request.type==0 && request.count==1 && request.xuids.empty())
    request.xuids.push_back(request.pivot);
  NativeService::Read(std::move(request),std::move(completion));
}
}
