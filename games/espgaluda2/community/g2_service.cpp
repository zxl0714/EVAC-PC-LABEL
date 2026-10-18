// CommunityService worker pattern adapted from Mushihimesama-Futari-PC
// 7dced5979076329db71b1296c81944c83fb72b48, community_runtime.cpp.
// G2 schema and score-before-replay workflow are independently adapted.
#include "community/g2_service.h"
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
uint32_t Unsigned(const Json& value, uint32_t cap = UINT32_MAX) {
  Require(value.is_number_integer());
  auto result = value.get<int64_t>();
  Require(result >= 0 && uint64_t(result) <= cap);
  return uint32_t(result);
}
std::string Nickname(std::string_view value) {
  auto result = cave::community::NormalizeNickname(value);
  Require(cave::community::ValidNickname(result));
  return result;
}
std::string Build(std::string value) { Require(!value.empty() && value.size() <= 96); return value; }
std::string GameEndpoint(std::string endpoint) {
  Require(cave::community::ValidEndpoint(endpoint));
  while (endpoint.ends_with('/')) endpoint.pop_back();
  return endpoint + "/games/espgaluda2";
}
std::string Encode(std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}
std::string Hex(std::span<const unsigned char> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
std::string Reference() {
  unsigned char bytes[16];
  Require(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
  return Hex(bytes);
}
bool ValidReference(const Json& value) {
  if (!value.is_string()) return false;
  const auto& text = value.get_ref<const std::string&>();
  return text.size() == 32 && text.find_first_not_of("0123456789abcdef") == text.npos;
}
bool ValidHash(std::string_view value) {
  return value.size()==64 && value.find_first_not_of("0123456789abcdef")==value.npos;
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
uint32_t Error(const cave::community::HttpResult& result) {
  if (result) return 0;
  if (result.error == ERROR_CANCELLED || result.error == kCancelled) return kCancelled;
  return result.error ? result.error : result.status ? kInvalid : kOffline;
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

G2Community::G2Community(std::string endpoint, std::string nickname, std::string build)
    : nickname_(Nickname(nickname)), build_(Build(std::move(build))), client_(GameEndpoint(std::move(endpoint))) {
  native_names_[kCommunityLocalXuid] = nickname_;
  worker_ = std::thread([this] { Run(); });
}
G2Community::~G2Community() { Stop(); }
void G2Community::Stop() {
  std::lock_guard stop(stop_mutex_);
  stopping_ = true;
  std::deque<Task> abandoned;
  { std::lock_guard lock(mutex_); abandoned.swap(tasks_); }
  client_.Stop();
  wake_.notify_all();
  for (auto& task : abandoned) task.fail(kCancelled);
  if (worker_.joinable()) worker_.join();
}
void G2Community::Enqueue(Task task) {
  uint32_t error = 0;
  {
    std::lock_guard lock(mutex_);
    if (stopping_) error = kCancelled;
    else if (tasks_.size() >= 8) error = ERROR_BUSY;
    else tasks_.push_back(std::move(task));
  }
  if (error) task.fail(error); else wake_.notify_one();
}
void G2Community::Run() {
  for (;;) {
    Task task;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (stopping_) return;
      task = std::move(tasks_.front()); tasks_.pop_front();
    }
    if (Clock::now() >= task.deadline) { task.fail(kTimeout); continue; }
    try { task.work(task.deadline); }
    catch (const std::exception&) {
      REXLOG_WARN("G2 community request rejected: invalid native or server data");
      task.fail(kInvalid);
    }
  }
}
cave::community::HttpResult G2Community::Http(std::string path, Json body, Clock::time_point deadline) {
  if (stopping_) return {0, kCancelled, {}};
  auto request = client_.Request(std::move(path), std::move(body));
  if (request.wait_until(deadline) != std::future_status::ready) return {0, kTimeout, {}};
  auto result = request.get();
  if (result && (result.body.value("protocol", 0) != 1 || result.body.value("title_id", "") != "435A07D8"))
    return {result.status, kInvalid, {}};
  return result;
}
std::string G2Community::DisplayName(uint64_t xuid) const {
  std::lock_guard lock(names_mutex_);
  auto found = native_names_.find(xuid);
  return found == native_names_.end() ? std::string() : found->second;
}
uint64_t G2Community::NativeToken(const std::string& nickname) {
  std::lock_guard lock(names_mutex_);
  for (const auto& [token, name] : native_names_) if (name == nickname) return token;
  Require(native_names_.size() < 4096);
  const auto token = next_token_++;
  native_names_[token] = nickname;
  return token;
}
void G2Community::Read(CommunityRead request, ReadCompletion completion) {
  Enqueue({Clock::now() + std::chrono::seconds(10),
      [this, request = std::move(request), completion](auto deadline) { completion(ReadNow(request, deadline)); },
      [completion](uint32_t error) { completion({error, {}}); }});
}
CommunityResult G2Community::ReadNow(const CommunityRead& request, Clock::time_point deadline) {
  Require(!request.specs.empty() && request.specs.size() <= 16 && request.count >= 1 && request.count <= 100 && request.xuids.size() <= 100);
  CommunityResult result;
  for (const auto& spec : request.specs) {
    Require(View(spec.view) && spec.columns.size() <= 64 && request.type <= 1);
    auto path = "/leaderboard?view=" + std::to_string(spec.view) + "&rules=standard&count=" + std::to_string(request.count);
    if (!request.xuids.empty()) {
      size_t known = 0;
      for (auto xuid : request.xuids) {
        auto name = DisplayName(xuid);
        if (!name.empty()) { path += "&name=" + Encode(name); ++known; }
      }
      if (!known) { result.views.push_back({spec.view, 0, {}}); continue; }
    } else if (request.type == 0) {
      auto name = DisplayName(request.pivot);
      if (name.empty()) { result.views.push_back({spec.view, 0, {}}); continue; }
      path += "&around=" + Encode(name);
    } else path += "&start=" + std::to_string(std::clamp(request.pivot, uint64_t(1), uint64_t(1000000)));
    auto reply = Http(path, nullptr, deadline);
    if (auto error = Error(reply)) return {error, {}};
    Require(Unsigned(reply.body.at("view")) == spec.view);
    CommunityView view{spec.view, Unsigned(reply.body.at("total"), 100000), {}};
    const auto& rows = reply.body.at("rows");
    Require(rows.is_array() && rows.size() <= (request.xuids.empty() ? request.count : request.xuids.size()));
    for (const auto& entry : rows) {
      Require(ValidReference(entry.at("id")));
      auto name = entry.at("nickname").get<std::string>();
      Require(cave::community::ValidNickname(name) && name == Nickname(name));
      CommunityRow row;
      row.nickname = name; row.native_tag = CommunityNativeName(name); row.xuid = NativeToken(name);
      row.rank = Unsigned(entry.at("rank"), view.total); Require(row.rank > 0);
      row.rating = Decimal(entry.at("score")); Require(row.rating >= 0);
      ScoreReference reference;
      reference.id=entry.at("id").get<std::string>();
      reference.properties=entry.at("columns");
      if(!entry.at("replay_id").is_null()) {
        Require(ValidReference(entry.at("replay_id")) && entry.at("replay_format")==kReplayFormat);
        reference.replay=entry.at("replay_id").get<std::string>();
        reference.hash=entry.at("replay_sha256").get<std::string>();
        reference.bytes=Unsigned(entry.at("replay_size"),kReplayMaxBytes);
        Require(ValidHash(reference.hash) && reference.bytes>kReplayHeaderBytes);
      }
      row.columns = DecodeCommunityColumns(reference.properties,reference.bytes);
      downloads_[{spec.view,row.xuid}]=std::move(reference);
      view.rows.push_back(std::move(row));
    }
    // An explicit XUserReadStats lookup has a stats row even when rank is 0.
    // G2's own-rank reader (820A9700) dereferences the first row whenever the
    // view exists, without testing row count. This is not ranked enumeration:
    // defaults remain local and never create a server/public-board score.
    for (auto xuid : request.xuids) {
      auto name = DisplayName(xuid);
      if (name.empty() || std::any_of(view.rows.begin(), view.rows.end(),
          [xuid](const auto& row) { return row.xuid == xuid; })) continue;
      CommunityRow unranked{xuid, 0, 0, name, {}};
      unranked.native_tag = CommunityNativeName(name);
      for (auto id : spec.columns) unranked.columns.push_back({id, (id >= 1 && id <= 6) || id == 15 ? 2u : 1u, 0, 0});
      view.rows.push_back(std::move(unranked));
    }
    REXLOG_INFO("G2 community native read: view={} rows={} total={}", spec.view, view.rows.size(), view.total);
    result.views.push_back(std::move(view));
  }
  return result;
}
void G2Community::Write(std::vector<CommunityWrite> views, WriteCompletion completion) {
  Enqueue({Clock::now() + std::chrono::seconds(10),
      [this, views = std::move(views), completion](auto deadline) { completion(WriteNow(views, deadline)); },
      [completion](uint32_t error) { completion(error); }});
}
uint32_t G2Community::WriteNow(const std::vector<CommunityWrite>& views, Clock::time_point deadline) try {
  Require(!views.empty() && views.size() <= 16);
  std::vector<Json> submissions;
  std::set<uint32_t> ids;
  // Validate the whole native batch before sending any request.
  for (const auto& view : views) {
    uploads_.erase(view.view);
    Require(ids.insert(view.view).second);
    auto body = EncodeCommunityScore(view);
    body.update({{"protocol", 1}, {"title_id", "435A07D8"}, {"submission", Reference()},
                 {"nickname", nickname_}, {"build", build_}});
    submissions.push_back(std::move(body));
  }
  const auto run=RunKey(submissions.front().at("columns"));
  for(const auto& body:submissions)Require(RunKey(body.at("columns"))==run);
  if(run.empty() || run!=upload_run_)uploads_.clear();
  upload_run_=run;
  for (auto& body : submissions) {
    auto reply = Http("/scores", body, deadline);
    if (auto error = Error(reply)) { uploads_.clear(); return error; }
    Require(ValidReference(reply.body.at("id")));
    const auto view=body.at("view").get<uint32_t>();
    if(!run.empty())uploads_[view]={reply.body.at("id").get<std::string>(),{},{},0,body.at("columns")};
    REXLOG_INFO("G2 community native score accepted: view={}", view);
  }
  return 0;
} catch(...) {
  uploads_.clear();
  throw;
}
void G2Community::Estimate(std::vector<CommunityRank> request, RankCompletion completion) {
  Enqueue({Clock::now() + std::chrono::seconds(10),
      [this, request = std::move(request), completion](auto deadline) { completion(EstimateNow(request, deadline)); },
      [completion](uint32_t error) { completion({error, {}}); }});
}
CommunityRankResult G2Community::EstimateNow(const std::vector<CommunityRank>& request, Clock::time_point deadline) {
  Require(!request.empty() && request.size() <= 16);
  CommunityRankResult result;
  for (const auto& entry : request) {
    Require(View(entry.view) && entry.rating >= 0);
    auto reply = Http("/estimate?view=" + std::to_string(entry.view) + "&rules=standard&score=" +
        std::to_string(entry.rating) + "&name=" + Encode(nickname_), nullptr, deadline);
    if (auto error = Error(reply)) return {error, {}};
    Require(Unsigned(reply.body.at("view")) == entry.view);
    const auto rank = Unsigned(reply.body.at("rank"), 100001); Require(rank > 0);
    result.ranks.push_back(rank);
  }
  return result;
}
void G2Community::Replay(CommunityReplay request, ReplayCompletion completion) {
  Enqueue({Clock::now()+std::chrono::seconds(10),
      [this,request=std::move(request),completion](auto deadline) { completion(ReplayNow(request,deadline)); },
      [completion](uint32_t error) { completion({error,{}}); }});
}
CommunityReplayResult G2Community::ReplayNow(const CommunityReplay& request,Clock::time_point deadline) {
  Require(View(request.view) && request.capacity>kReplayHeaderBytes && request.capacity<=kReplayMaxBytes);
  if(request.upload) {
    Require(request.xuid==kCommunityLocalXuid && request.record.size()==request.capacity);
    auto found=uploads_.find(request.view);
    Require(found!=uploads_.end());
    const auto& reference=found->second;
    REXLOG_INFO("G2 native replay upload: view={} bytes={}",request.view,request.record.size());
    try {
      MatchReplay(reference.properties,InspectReplay(request.record),request.record.size());
    } catch(...) {
      // Bounded numeric envelope diagnostics, never compressed input or names.
      // The original title can update its local save buffer after the network
      // attempt, so inspecting that later file cannot prove what was uploaded.
      if(request.record.size()>=kReplayHeaderBytes) {
        auto word=[&](size_t at) {const auto* b=request.record.data()+at;
          return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];};
        auto wide=[&](size_t at) {return uint64_t(word(at))<<32|word(at+4);};
        REXLOG_WARN("G2 replay envelope rejected: view={} bytes={} local_checksum={} version={} utc={} score={} character={} payload={}",
            request.view,request.record.size(),word(0),word(4),wide(8),wide(16),word(36),word(96));
      }
      throw;
    }
    const auto hash=ReplaySha256(request.record);
    auto reply=Http("/replays",{{"protocol",1},{"title_id","435A07D8"},{"score_id",reference.id},
        {"nickname",nickname_},{"format",kReplayFormat},{"sha256",hash},{"data",ReplayBase64(request.record)}},deadline);
    if(auto error=Error(reply))return {error,{}};
    Require(ValidReference(reply.body.at("id")) && reply.body.at("score_id")==reference.id &&
        reply.body.at("sha256")==hash && Unsigned(reply.body.at("bytes"))==request.record.size());
    REXLOG_INFO("G2 native replay accepted: view={} bytes={} sha256={}",request.view,request.record.size(),hash);
    return {};
  }
  Require(request.record.empty());
  auto found=downloads_.find({request.view,request.xuid});
  if(found==downloads_.end() || found->second.replay.empty())return {ERROR_FILE_NOT_FOUND,{}};
  const auto& reference=found->second;
  if(reference.bytes>request.capacity)return {ERROR_INSUFFICIENT_BUFFER,{}};
  auto reply=Http("/replays/"+reference.replay,nullptr,deadline);
  if(auto error=Error(reply))return {error,{}};
  Require(reply.body.at("id")==reference.replay && reply.body.at("format")==kReplayFormat &&
      reply.body.at("sha256")==reference.hash && Unsigned(reply.body.at("bytes"))==reference.bytes);
  auto record=ReplayUnbase64(reply.body.at("data").get<std::string>());
  Require(record.size()==reference.bytes && ReplaySha256(record)==reference.hash);
  MatchReplay(reference.properties,InspectReplay(record),record.size());
  REXLOG_INFO("G2 native replay downloaded: view={} bytes={} sha256={}",request.view,record.size(),reference.hash);
  return {0,std::move(record)};
}
}  // namespace espgaluda2
