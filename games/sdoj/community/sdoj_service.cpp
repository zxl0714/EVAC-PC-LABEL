#include "community/sdoj_service.h"
#include "community/replay_format.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>

namespace sdoj {
using namespace rex::system::xam;
using Json=nlohmann::json;
namespace {
void Check(bool ok){if(!ok)throw std::runtime_error("Invalid SDOJ community data");}
constexpr std::pair<uint32_t,uint32_t> kTypes[]={{0x20000004,2},{0x20000002,2},
    {0x10000007,1},{0x10000006,1},{0x10000005,1},{0x10000001,1},{0x10008011,1}};
uint32_t Type(uint32_t id) {
  for(const auto& [key,type]:kTypes)if(id==key)return type;
  return 0;
}
int64_t Number(const Json& value,uint32_t type) {
  if(type==1){Check(value.is_number_integer());return value.get<int64_t>();}
  Check(value.is_string());const auto& text=value.get_ref<const std::string&>();
  int64_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
  Check(!text.empty() && text.size()<=20 && parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size());
  return result;
}
std::map<uint32_t,int64_t> Properties(const Json& properties) {
  Check(properties.is_array() && properties.size()>=6 && properties.size()<=7);
  std::map<uint32_t,int64_t> result;
  for(const auto& item:properties) {
    Check(item.at("id").is_number_integer() && item.at("type").is_number_integer());
    const auto id64=item.at("id").get<int64_t>();Check(id64>=0 && id64<=UINT32_MAX);
    const auto id=uint32_t(id64),type=Type(id);Check(type && item.at("type")==type);
    auto value=Number(item.at("value"),type);
    Check(result.emplace(id,value).second);
    if(type==1)Check(value>=0 && value<=INT32_MAX);
  }
  for(const auto& [id,type]:kTypes)if(id!=0x10008011)Check(result.contains(id));
  Check(result.at(0x20000004)>=0);
  const auto packed=result.at(0x10000007);
  Check(packed<=0x32 && (packed&15)<=2);
  for(auto id:{0x10000001,0x10000005,0x10000006})Check(result.at(id)<=0xffff);
  NativeFiletime(result.at(0x20000002));
  if(result.contains(0x10008011)) {
    const auto size=result.at(0x10008011);
    Check(!size || (size>kReplayHeaderBytes && size<=kReplayMaxBytes));
  }
  return result;
}
bool View(uint32_t view){return ViewCharacterType(view)!=UINT32_MAX;}
uint32_t ColumnType(uint16_t id){return id==2 || id==4?2:1;}
std::string RunKey(const Json& properties) {
  const auto p=Properties(properties);
  return std::to_string(p.at(0x20000004))+"/"+std::to_string(p.at(0x20000002))+"/"+std::to_string(p.at(0x10000007));
}
void MatchReplay(const Json& properties,std::span<const uint8_t> record) {
  const auto p=Properties(properties);const auto info=InspectReplay(record);
  Check(p.at(0x20000004)==info.score && p.at(0x20000002)==info.timestamp &&
      p.at(0x10000007)==info.character_type && p.at(0x10000006)==info.stage &&
      p.at(0x10000005)==info.continues && p.at(0x10000001)==info.option);
  if(p.contains(0x10008011))Check(!p.at(0x10008011) || p.at(0x10008011)==int64_t(record.size()));
}
cave::community::NativeSchema Schema() {
  return {.game="sdoj",.title="435A07E7",.replay_namespace="sdoj-replay",
      .replay_format=kReplayFormat,.replay_header=kReplayHeaderBytes,.replay_max=kReplayMaxBytes,
      .valid_view=View,.encode_score=EncodeCommunityScore,.decode_columns=DecodeCommunityColumns,
      .column_type=ColumnType,.native_name=cave::community::NativeLegacyName,
      .run_key=RunKey,.match_replay=MatchReplay,.storage_size_property=0x10008011};
}
}
Json EncodeCommunityScore(const CommunityWrite& view) {
  Check(View(view.view));Json columns=Json::array();
  for(const auto& col:view.columns) {
    Check(Type(col.id)==col.type && col.type);
    Json value=col.type==2?Json(std::to_string(col.integer)):Json(col.integer);
    columns.push_back({{"id",col.id},{"type",col.type},{"value",std::move(value)}});
  }
  const auto props=Properties(columns);
  Check(props.at(0x10000007)==ViewCharacterType(view.view));
  return {{"view",view.view},{"rules","standard"},{"score",std::to_string(props.at(0x20000004))},{"columns",std::move(columns)}};
}
std::vector<CommunityColumn> DecodeCommunityColumns(const Json& properties,uint32_t replay_bytes) {
  const auto props=Properties(properties);std::vector<CommunityColumn> result;
  Check(!replay_bytes || (replay_bytes>kReplayHeaderBytes && replay_bytes<=kReplayMaxBytes));
  for(const auto& [id,value]:props)result.push_back({id&0xffff,Type(id),value,0});
  result.push_back({0xfffa,1,replay_bytes,0});return result;
}
SdojCommunity::SdojCommunity(std::string endpoint,std::string nickname,std::string build)
    :NativeService(Schema(),std::move(endpoint),std::move(nickname),std::move(build)) {}
}
