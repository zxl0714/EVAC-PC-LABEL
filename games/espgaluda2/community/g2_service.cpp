// CommunityService worker pattern adapted from Mushihimesama-Futari-PC
// 7dced5979076329db71b1296c81944c83fb72b48, community_runtime.cpp.
// G2 schema and score-before-replay workflow are independently adapted.
#include "community/g2_service.h"
#include "community/native_service.h"
#include "community/replay_format.h"
#include <rex/logging.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>

namespace espgaluda2 {
using namespace rex::system::xam;
using Json = nlohmann::json;
namespace {
constexpr uint32_t kCancelled = 995, kInvalid = 13, kOffline = 1231, kTimeout = 1460;
void Require(bool value) { if (!value) throw std::runtime_error("Invalid G2 community data"); }
bool View(uint32_t view) { return view >= 1 && view <= 84; }
int64_t Decimal(const Json& value) {
  Require(value.is_string());
  const auto& text = value.get_ref<const std::string&>();
  int64_t result{};
  auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  Require(!text.empty() && text.size() <= 20 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
  return result;
}
uint32_t Unsigned(const Json& value,uint32_t cap=UINT32_MAX) {
  Require(value.is_number_integer());
  const auto result=value.get<int64_t>();
  Require(result>=0 && uint64_t(result)<=cap);
  return uint32_t(result);
}
std::string Hex(std::span<const unsigned char> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
const Json* Property(const Json& properties,uint32_t id) {
  for(const auto& property:properties) if(property.at("id")==id)return &property;
  return nullptr;
}
std::string RunKey(const Json& properties) {
  const auto *score=Property(properties,0x20000001), *time=Property(properties,0x20000011),
      *character=Property(properties,0x10000008);
  if(!score || !time || !character)return {};
  Require(score->at("type")==2 && time->at("type")==2 && character->at("type")==1);
  return score->at("value").get<std::string>()+"/"+time->at("value").get<std::string>()+"/"+
      std::to_string(character->at("value").get<int32_t>());
}
void MatchReplay(const Json& properties,const ReplayInfo& info,size_t bytes) {
  auto match=[&](uint32_t id,uint32_t type,int64_t value) {
    const auto* property=Property(properties,id);
    Require(property && property->at("type")==type);
    Require((type==2?Decimal(property->at("value")):property->at("value").get<int64_t>())==value);
  };
  match(0x20000001,2,info.score);
  match(0x20000011,2,int64_t(info.time));
  match(0x10000008,1,info.character);
  match(0x10008011,1,int64_t(bytes));
}
}  // namespace

Json EncodeCommunityScore(const CommunityWrite& view) {
  Require(View(view.view) && !view.columns.empty() && view.columns.size() <= 64);
  Json columns = Json::array();
  std::set<uint32_t> ids;
  const CommunityColumn* score = nullptr;
  for (const auto& col : view.columns) {
    Require(ids.insert(col.id).second);
    Json value;
    if (col.type == 1) {
      Require(col.integer >= INT32_MIN && col.integer <= INT32_MAX);
      value = col.integer;
    } else if (col.type == 2 || col.type == 7) value = std::to_string(col.integer);
    else throw std::runtime_error("Unsupported G2 native property type");
    if (col.id == 0x20000001 && col.type == 2) score = &col;
    // Preserve full write IDs. Read columns are a different namespace.
    columns.push_back({{"id", col.id}, {"type", col.type}, {"value", std::move(value)}});
  }
  Require(score && score->integer >= 0);
  return {{"view", view.view}, {"rules", "standard"}, {"score", std::to_string(score->integer)},
          {"columns", std::move(columns)}};
}

std::vector<CommunityColumn> DecodeCommunityColumns(const Json& properties,uint32_t replay_bytes) {
  Require(properties.is_array() && properties.size() <= 64);
  std::vector<CommunityColumn> result;
  std::set<uint32_t> ids;
  for (const auto& property : properties) {
    const auto id = Unsigned(property.at("id"));
    const auto type = Unsigned(property.at("type"), 7);
    Require(ids.insert(id).second);
    int64_t value{};
    if (type == 1) {
      Require(property.at("value").is_number_integer());
      value = property.at("value").get<int64_t>();
      Require(value >= INT32_MIN && value <= INT32_MAX);
    } else if (type == 2 || type == 7) value = Decimal(property.at("value"));
    else throw std::runtime_error("Unsupported G2 response property type");
    // 820A79F0 builds write properties; the reader's 820A8C74..820A8DB8
    // cases populate the corresponding native row fields in a different order.
    // Total score is row.rating, not column 1. In particular character is
    // property 10000008 -> column 7, not the low 16 bits of its property ID.
    if (id >= 0x20000002 && id <= 0x20000007) {
      Require(type == 2);
      result.push_back({id - 0x20000001, 2, value, 0});
    } else if (id == 0x20000011) {
      Require(type == 2);
      result.push_back({15, 2, value, 0});
    } else {
      // Match the native row offsets, not the property declaration order:
      // columns 7..14 -> offsets 80,84,88,92,76,100,96,104.
      // Writer 820A79F0 reads those fields at +1384 before constructing
      // properties; reader 820A8C74..820A8D00 restores these exact offsets.
      constexpr std::array<uint32_t, 8> metadata{
          0x10000008, 0x1000000c, 0x10000010, 0x1000000a,
          0x1000000f, 0x10000009, 0x1000000b, 0x1000000d};
      auto found = std::find(metadata.begin(), metadata.end(), id);
      if (found != metadata.end()) {
        Require(type == 1);
        result.push_back({uint32_t(7 + (found - metadata.begin())), 1, value, 0});
      }
    }
  }
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  Require(!replay_bytes || (replay_bytes>kReplayHeaderBytes && replay_bytes<=kReplayMaxBytes));
  // Native FFfa is the downloadable byte count, not the raw upload property.
  // It stays zero unless the server confirms an attachment for this exact row.
  result.push_back({0xfffa, 1, replay_bytes, 0});
  return result;
}

std::string CommunityNativeName(std::string_view nickname) {
  if (!nickname.empty() && std::all_of(nickname.begin(), nickname.end(), [](unsigned char c) { return c >= 32 && c <= 126; }))
    return std::string(nickname.substr(0, 15));
  unsigned char hash[32];
  Require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<char*>(nickname.data())), ULONG(nickname.size()), hash, 32) >= 0);
  return "Player-" + Hex(hash).substr(0, 8);
}

namespace {
uint32_t UnrankedColumnType(uint16_t id) {return (id>=1 && id<=6) || id==15?2u:1u;}
void MatchNativeRecording(const Json& properties,std::span<const uint8_t> record) {
  MatchReplay(properties,InspectReplay(record),record.size());
}
cave::community::NativeSchema Schema() {
  return {.game="espgaluda2",.title="435A07D8",.replay_namespace="espgaluda2-replay",
      .replay_format=kReplayFormat,.replay_header=kReplayHeaderBytes,.replay_max=kReplayMaxBytes,
      .valid_view=View,.encode_score=EncodeCommunityScore,.decode_columns=DecodeCommunityColumns,
      .column_type=UnrankedColumnType,.native_name=CommunityNativeName,
      .run_key=RunKey,.match_replay=MatchNativeRecording};
}
}
G2Community::G2Community(std::string endpoint,std::string nickname,std::string build)
    :NativeService(Schema(),std::move(endpoint),std::move(nickname),std::move(build)) {}
}  // namespace espgaluda2
