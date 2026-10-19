#include "community_runtime.h"
#include "replay_format.h"
#include <rex/logging.h>
#include <rex/filesystem.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

using namespace rex::system::xam;
namespace {
constexpr uint64_t kLocalNativeXuid=0xB13EBABEBABEBABEull;
constexpr uint32_t kCancelled=995,kTimeout=1460,kInvalidData=13,kOffline=1231;
std::string EndpointIdentity(std::string endpoint) {
  while(!endpoint.empty() && endpoint.back()=='/')endpoint.pop_back();
  return endpoint;
}
std::string RandomReference() {
  unsigned char bytes[16];
  if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) throw std::runtime_error("Random source unavailable");
  std::ostringstream out; out<<std::hex<<std::setfill('0'); for(auto byte:bytes)out<<std::setw(2)<<unsigned(byte); return out.str();
}
bool IsReference(const std::string& id) {return id.size()==32 && id.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string Encode(const std::string& value) {
  constexpr char hex[]="0123456789ABCDEF"; std::string out;
  for(unsigned char c:value) {
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
void SaveJson(const std::filesystem::path& path,const nlohmann::json& data) {
  std::filesystem::create_directories(path.parent_path());
  auto temporary=path; temporary+=L".tmp";
  {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.exceptions(std::ios::badbit|std::ios::failbit);file<<data.dump(2);file.flush();file.close();}
  if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Community journal save failed");
}
uint32_t ResultError(const FutariHttpResult& result) {
  if(result) return 0;
  if(result.error==kCancelled || result.error==ERROR_CANCELLED) return kCancelled;
  if(result.error) return result.error;
  return result.status ? kInvalidData : kOffline;
}
std::string Hash(std::span<const uint8_t> bytes) {
  unsigned char digest[32]{};
  if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest,32)<0)
    throw std::runtime_error("Replay hash failed");
  std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto c:digest)out<<std::setw(2)<<unsigned(c);return out.str();
}
std::string Base64(std::span<const uint8_t> bytes) {
  DWORD count=0;
  constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
  if(!CryptBinaryToStringA(bytes.data(),DWORD(bytes.size()),flags,nullptr,&count))throw std::runtime_error("Replay encoding failed");
  std::string out(count,'\0');
  if(!CryptBinaryToStringA(bytes.data(),DWORD(bytes.size()),flags,out.data(),&count))throw std::runtime_error("Replay encoding failed");
  // CryptBinaryToString excludes the terminator from the returned character count.
  out.resize(count);while(!out.empty() && out.back()=='\0')out.pop_back();return out;
}
std::vector<uint8_t> Unbase64(const std::string& text) {
  if(text.empty() || text.size()>(kFutariMaxReplayBytes+2)/3*4 || text.size()%4
      || text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::string::npos)
    throw std::runtime_error("Invalid replay encoding");
  DWORD count=0;
  if(!CryptStringToBinaryA(text.data(),DWORD(text.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&count,nullptr,nullptr)
      || !count || count>kFutariMaxReplayBytes)throw std::runtime_error("Invalid replay encoding");
  std::vector<uint8_t> out(count);
  if(!CryptStringToBinaryA(text.data(),DWORD(text.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,out.data(),&count,nullptr,nullptr))
    throw std::runtime_error("Invalid replay encoding");
  out.resize(count);
  if(Base64(out)!=text)throw std::runtime_error("Noncanonical replay encoding");
  return out;
}
void LimitOutbox(const std::filesystem::path& directory,size_t limit) {
  std::filesystem::create_directories(directory);
  size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file())++count;
  if(count>=limit)throw std::runtime_error("Community outbox full");
}
}

std::string FutariNativeNickname(std::string name) {
  // The native gamer-tag font only draws Latin characters, even though other
  // title UI fonts draw Japanese. This bounded display alias is not identity:
  // scores, lookups and the Unicode gamer card always use the full nickname.
  if(!name.empty() && std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=0x20 && c<=0x7e;})) {
    name.resize(std::min(size_t(15),name.size()));return name;
  }
  return "Player-"+Hash({reinterpret_cast<const uint8_t*>(name.data()),name.size()}).substr(0,8);
}
std::string FutariCommunityBuildIdentity() {
  try {
    auto path=rex::filesystem::GetExecutableFolder()/"BUILDINFO.json";
    if(std::filesystem::file_size(path)>32768)return "development";
    std::ifstream file(path);auto info=nlohmann::json::parse(file);
    auto build=info.at("GitCommit").get<std::string>();
    if(build.size()>80)return "development";
    if(info.value("Dirty",true))build+="+dirty";
    return build;
  }catch(...){return "development";}
}

FutariCommunity::FutariCommunity(std::string endpoint,std::string nickname,std::string build,std::filesystem::path user_data)
    :endpoint_(EndpointIdentity(std::move(endpoint))),client_(endpoint_),nickname_(FutariNormalizeNickname(nickname)),build_(std::move(build)),user_data_(std::filesystem::absolute(user_data)) {
  if(!FutariValidNickname(nickname_))throw std::runtime_error("Invalid community nickname");
  native_names_[kLocalNativeXuid]=nickname_;
  worker_=std::thread([this]{Run();});
}
FutariCommunity::~FutariCommunity(){Stop();}
nlohmann::json FutariCommunity::JournalOrigin() const {
  return {{"game","futari"},{"endpoint",endpoint_}};
}
bool FutariCommunity::JournalMatches(const nlohmann::json& journal) const {
  auto origin=journal.find("origin");
  return origin!=journal.end() && origin->is_object() && *origin==JournalOrigin();
}
void FutariCommunity::Stop() {
  std::lock_guard stop_lock(stop_mutex_);
  stopping_=true;
  std::deque<Task> abandoned;
  {std::lock_guard lock(mutex_);abandoned.swap(tasks_);}
  client_.Stop();wake_.notify_all();
  for(auto& task:abandoned)task.cancel();
  if(worker_.joinable())worker_.join();
}
void FutariCommunity::Enqueue(Task task) {
  bool rejected=false;
  {std::lock_guard lock(mutex_);if(stopping_||tasks_.size()>=8)rejected=true;else tasks_.push_back(std::move(task));}
  if(rejected)task.cancel();else wake_.notify_one();
}
void FutariCommunity::Run() {
  try{ArchiveUnboundReplays();}
  catch(const std::exception&){REXLOG_WARN("Community staging recovery deferred; original journals retained");}
  auto retry=Clock::now();
  while(!stopping_) {
    Task task;
    {
      std::unique_lock lock(mutex_);
      wake_.wait_until(lock,retry,[this]{return stopping_||!tasks_.empty();});
      if(stopping_)break;
      if(!tasks_.empty()){task=std::move(tasks_.front());tasks_.pop_front();}
    }
    if(task.work) {
      if(Clock::now()>task.deadline){task.cancel();continue;}
      try{task.work(task.deadline);}catch(const std::exception&){REXLOG_WARN("Community task rejected: invalid data or journal IO");task.cancel();}
    } else {
      try{RetryOne();}catch(const std::exception&){REXLOG_WARN("Community queued score retry failed; journal retained");}
      retry=Clock::now()+std::chrono::seconds(30);
    }
  }
}
FutariHttpResult FutariCommunity::Http(std::string path,nlohmann::json body,Clock::time_point deadline) {
  if(stopping_)return {0,kCancelled,{}};
  auto future=client_.Request(std::move(path),std::move(body));
  if(future.wait_until(deadline)!=std::future_status::ready)return {0,kTimeout,{}};
  return future.get();
}
std::string FutariCommunity::NativeName(uint64_t xuid) {
  return DisplayName(xuid);
}
std::string FutariCommunity::DisplayName(uint64_t xuid) const {
  std::lock_guard lock(names_mutex_);
  auto it=native_names_.find(xuid);return it==native_names_.end()?std::string():it->second;
}
uint64_t FutariCommunity::NativeToken(const std::string& name) {
  std::lock_guard lock(names_mutex_);
  for(const auto& [token,nickname]:native_names_)if(nickname==name)return token;
  if(native_names_.size()>=10000)throw std::runtime_error("Native row cache full");
  auto token=next_token_++;native_names_.emplace(token,name);return token;
}
void FutariCommunity::Read(CommunityRead request,ReadCompletion completion) {
  Enqueue({Clock::now()+std::chrono::seconds(10),
    [this,request=std::move(request),completion](auto deadline){completion(ReadNow(request,deadline));},
    [completion]{completion({kCancelled,{}});}});
}
CommunityResult FutariCommunity::ReadNow(const CommunityRead& request,Clock::time_point deadline) {
  CommunityResult result;
  if(request.specs.empty()||request.specs.size()>16||request.count>100||!request.count)return {kInvalidData,{}};
  for(const auto& spec:request.specs) {
    if(std::find(request.xuids.begin(),request.xuids.end(),kLocalNativeXuid)!=request.xuids.end())
      accepted_scores_.erase(spec.view);
    auto path="/leaderboard?view="+std::to_string(spec.view)+"&rules=standard&count="+std::to_string(request.count);
    if(!request.xuids.empty()) {
      for(auto xuid:request.xuids){auto name=NativeName(xuid);if(!name.empty())path+="&name="+Encode(name);}
      if(path.find("&name=")==std::string::npos){result.views.push_back({spec.view,0,{}});continue;}
    } else if(request.type==0) {
      auto name=NativeName(request.pivot); if(name.empty()){result.views.push_back({spec.view,0,{}});continue;}
      path+="&around="+Encode(name);
    } else path+="&start="+std::to_string(std::clamp(uint64_t(request.pivot),uint64_t(1),uint64_t(1000000)));
    std::ostringstream requested_columns;
    for(auto column:spec.columns)requested_columns<<std::hex<<column<<',';
    REXLOG_INFO("Community native read: view={} type={} count={} columns={}",
        spec.view,request.type,request.count,requested_columns.str());
    auto reply=Http(path,nullptr,deadline);if(auto error=ResultError(reply))return {error,{}};
    auto& body=reply.body;
    CommunityView view{spec.view,body.at("total").get<uint32_t>(),{}};
    if(body.at("view")!=spec.view||!body.at("rows").is_array()||body.at("rows").size()>request.count)return {kInvalidData,{}};
    for(auto& entry:body.at("rows")) {
      CommunityRow row;
      row.nickname=entry.at("nickname").get<std::string>();if(!FutariValidNickname(row.nickname))return {kInvalidData,{}};
      row.native_tag=FutariNativeNickname(row.nickname);
      row.xuid=NativeToken(row.nickname); row.rank=entry.at("rank").get<uint32_t>();
      ReplayReference reference{entry.at("id").get<std::string>(),{},entry.at("build").get<std::string>()};
      if(!IsReference(reference.score) || reference.build.empty() || reference.build.size()>96)return {kInvalidData,{}};
      if(!entry.at("replay_id").is_null()) {
        reference.replay=entry.at("replay_id").get<std::string>();
        if(!IsReference(reference.replay))return {kInvalidData,{}};
      }
      replay_references_[{spec.view,row.xuid}]=std::move(reference);
      uint32_t replay_size=0;
      if(!entry.at("replay_id").is_null() && entry.at("replay_format")=="futari-xstorage-v1") {
        replay_size=entry.at("replay_size").get<uint32_t>();
        if(replay_size<=kFutariStorageReplayHeaderBytes || replay_size>kFutariStorageReplayMaxBytes)
          return {kInvalidData,{}};
      }
      row.rating=std::stoll(entry.at("score").get<std::string>());
      auto& columns=entry.at("columns");if(!columns.is_array()||columns.size()>64)return {kInvalidData,{}};
      for(const auto& value:columns) {
        CommunityColumn col{value.at("id").get<uint32_t>(),value.at("type").get<uint32_t>(),0,0};
        if(col.type==1)col.integer=value.at("value").get<int32_t>();
        else if(col.type==2||col.type==7)col.integer=std::stoll(value.at("value").get<std::string>());
        else if(col.type==3||col.type==5)col.real=value.at("value").get<double>();
        else return {kInvalidData,{}};
        // The title advertises the local replay's size during score upload,
        // before any replay has been shared. Do not advertise a downloadable
        // native attachment just because that original property is nonzero.
        if((col.id&0xffff)==0x8011 && entry.at("replay_id").is_null())col.integer=0;
        // Native reader 8218E558 takes its download buffer size from view
        // column 8 (+184 in the eight-column array). It is attachment metadata,
        // not a copy of title property 8 from XSessionWriteStats. Only expose
        // the size of an accepted, compatible server attachment.
        // The native view is not the title's raw upload-property namespace:
        // column 1 is the replay-present byte (8218E558 -> row+48), tested by
        // the Info menu at 821AE60C. Rating remains the score in row+16.
        if(col.id==1 || col.id==8)continue;
        row.columns.push_back(col);
      }
      row.columns.push_back({1,1,replay_size?1:0,0});
      row.columns.push_back({8,1,replay_size,0});
      REXLOG_INFO("Community native row: view={} rank={} replay_bytes={}",
          spec.view,row.rank,replay_size);
      view.rows.push_back(std::move(row));
    }
    // XUserReadStats is an explicit user-stat lookup, not ranked enumeration.
    // An unranked user still has a zero-valued stats row (rank 0). Futari's
    // first-score comparison skips missing rows altogether (821C0D84), which
    // would otherwise prevent every new nickname from submitting its first run.
    // This default stays local; it is never inserted into the server board.
    for(auto xuid:request.xuids) {
      const auto name=NativeName(xuid);
      if(name.empty() || std::any_of(view.rows.begin(),view.rows.end(),[&](const auto& row){return row.xuid==xuid;}))continue;
      CommunityRow unranked{xuid,0,0,name,{}};
      unranked.native_tag=FutariNativeNickname(name);
      for(auto id:spec.columns)unranked.columns.push_back({id,(id==1||id==2)?2u:1u,0,0});
      view.rows.push_back(std::move(unranked));
      REXLOG_INFO("Community native unranked user stats: view={}",spec.view);
    }
    result.views.push_back(std::move(view));
  }
  return result;
}
void FutariCommunity::Write(std::vector<CommunityWrite> views,WriteCompletion completion) {
  Enqueue({Clock::now()+std::chrono::seconds(10),
    [this,views=std::move(views),completion](auto deadline){completion(WriteNow(views,deadline));},
    [completion]{completion(kCancelled);}});
}
uint32_t FutariCommunity::WriteNow(const std::vector<CommunityWrite>& views,Clock::time_point deadline) {
  if(views.empty()||views.size()>16)return kInvalidData;
  // A failed new run must never attach its replay to a previous run's score.
  for(const auto& view:views)accepted_scores_.erase(view.view);
  nlohmann::json batch={{"version",2},{"origin",JournalOrigin()},{"nickname",nickname_},{"scores",nlohmann::json::array()}};
  for(const auto& view:views) {
    // Futari's native stats writer (8218D230) stores the run's 64-bit score
    // in title property 0x20000001. 0x8000 is not a title score column.
    auto score=std::find_if(view.columns.begin(),view.columns.end(),[](auto& c){return c.id==0x20000001 && c.type==2;});
    if(score==view.columns.end()||score->integer<0) {
      REXLOG_WARN("Community native score schema not recognized: view={}; no submission made",view.view);
      return kInvalidData;
    }
    nlohmann::json columns=nlohmann::json::array();
    for(const auto& col:view.columns) {
      nlohmann::json value;
      if(col.type==1)value=int32_t(col.integer);
      else if(col.type==2||col.type==7)value=std::to_string(col.integer);
      else if(col.type==3||col.type==5)value=col.real;
      else return kInvalidData;
      columns.push_back({{"id",col.id&0xffff},{"type",col.type},{"value",value}});
    }
    nlohmann::json submission={{"protocol",1},{"title_id","435A07D7"},{"submission",RandomReference()},
      {"nickname",nickname_},{"view",view.view},{"rules","standard"},{"score",std::to_string(score->integer)},
      {"columns",std::move(columns)},{"build",build_}};
    auto staged=staged_replays_.find(view.view);
    auto attachment=std::find_if(view.columns.begin(),view.columns.end(),[](const auto& c){return (c.id&0xffff)==0x8011;});
    if(staged!=staged_replays_.end() && attachment!=view.columns.end() && attachment->type==1
        && attachment->integer==staged->second.size)
      submission["pending_replay"]=staged->second.reference;
    batch["scores"].push_back(std::move(submission));
  }
  auto directory=user_data_/"community"/"outbox";
  std::filesystem::create_directories(directory);
  size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file())++count;
  if(count>=128)return ERROR_DISK_FULL;
  auto path=directory/(RandomReference()+".json");
  SaveJson(path,batch);
  for(const auto& view:views)staged_replays_.erase(view.view);
  auto error=SendBatch(batch,path,deadline);
  if(!error)for(const auto& score:batch.at("scores"))accepted_scores_[score.at("view").get<uint32_t>()]=score;
  return error;
}
uint32_t FutariCommunity::SendBatch(nlohmann::json& batch,const std::filesystem::path& path,Clock::time_point deadline) {
  if(!JournalMatches(batch))return kInvalidData;
  for(auto& score:batch.at("scores")) {
    if(!score.contains("server_id")) {
      auto body=score;body.erase("pending_replay");
      auto reply=Http("/scores",std::move(body),deadline);if(auto error=ResultError(reply))return error;
      auto id=reply.body.at("id").get<std::string>();if(!IsReference(id))return kInvalidData;
      score["server_id"]=id;SaveJson(path,batch);
      REXLOG_INFO("Community score accepted: view={}",score.at("view").get<uint32_t>());
    }
    if(score.contains("pending_replay")) {
      auto reference=score.at("pending_replay").get<std::string>();if(!IsReference(reference))return kInvalidData;
      auto staged=user_data_/"community"/"pending-replays"/(reference+".json");
      if(std::filesystem::file_size(staged)>12*1024*1024)return kInvalidData;
      std::ifstream file(staged);auto body=nlohmann::json::parse(file);file.close();
      if(!JournalMatches(body) || body.at("nickname")!=score.at("nickname") || body.at("build")!=score.at("build")
          || body.at("mode")!="view-"+std::to_string(score.at("view").get<uint32_t>()))return kInvalidData;
      body["score_id"]=score.at("server_id");
      auto replay_path=user_data_/"community"/"replay-outbox"/(reference+".json");
      SaveJson(replay_path,body);
      if(auto error=SendReplay(body,replay_path,deadline))return error;
      score.erase("pending_replay");SaveJson(path,batch);
      std::filesystem::remove(staged);
    }
  }
  // Only the synchronous native write path installs the live run association;
  // background retries must not replace it with an older offline run.
  auto history=user_data_/"community"/"history";std::filesystem::create_directories(history);
  std::filesystem::rename(path,history/path.filename());
  return 0;
}

void FutariCommunity::Replay(CommunityReplay request,ReplayCompletion completion) {
  Enqueue({Clock::now()+std::chrono::seconds(10),
    [this,request=std::move(request),completion](auto deadline) {
      try{completion(ReplayNow(request,deadline));}
      catch(const std::exception&){REXLOG_WARN("Community replay rejected: validation or journal IO");completion({kInvalidData,{}});}
    },[completion]{completion({kCancelled,{}});}});
}
void FutariCommunity::Estimate(std::vector<CommunityRank> request,RankCompletion completion) {
  Enqueue({Clock::now()+std::chrono::seconds(10),[this,request=std::move(request),completion](Clock::time_point deadline) {
    CommunityRankResult result;
    if(request.empty() || request.size()>16){completion({kInvalidData,{}});return;}
    for(const auto& rank:request) {
      if(rank.rating<0){completion({kInvalidData,{}});return;}
      auto reply=Http("/estimate?view="+std::to_string(rank.view)+"&rules=standard&score="
          +std::to_string(rank.rating)+"&name="+Encode(nickname_),nullptr,deadline);
      if(auto error=ResultError(reply)){completion({error,{}});return;}
      auto value=reply.body.at("rank").get<uint32_t>();
      if(reply.body.at("view")!=rank.view || !value || value>100001){completion({kInvalidData,{}});return;}
      result.ranks.push_back(value);
      REXLOG_INFO("Community native rank estimate: view={} rank={}",rank.view,value);
    }
    completion(std::move(result));
  },[completion]{completion({kCancelled,{}});}});
}
CommunityReplayResult FutariCommunity::ReplayNow(const CommunityReplay& request,Clock::time_point deadline) {
  if(!request.capacity || request.capacity>kFutariMaxReplayBytes)return {kInvalidData,{}};
  const auto mode="view-"+std::to_string(request.view);
  if(request.upload) {
    if(request.xuid!=kLocalNativeXuid || request.record.size()!=request.capacity)return {kInvalidData,{}};
    auto found=accepted_scores_.find(request.view);
    // The network envelope differs from a local replaydata.bin slot. Preserve
    // the original 104-byte header and compressed stream without converting it.
    FutariValidateStorageReplay(request.record);
    const auto& payload=request.record;
    nlohmann::json body={{"protocol",1},{"title_id","435A07D7"},
      {"nickname",nickname_},{"format","futari-xstorage-v1"},{"build",build_},
      {"mode",mode},{"sha256",Hash(payload)},{"payload",Base64(payload)},{"origin",JournalOrigin()}};
    if(found==accepted_scores_.end()) {
      // Futari uploads its replay BEFORE XSessionWriteStats. Durably stage it,
      // then bind this exact reference into the following native score journal.
      // Unbound recordings are never published or matched after a restart.
      auto directory=user_data_/"community"/"pending-replays";
      const auto previous=staged_replays_.find(request.view);
      if(previous==staged_replays_.end())LimitOutbox(directory,8);
      auto reference=previous==staged_replays_.end()?RandomReference():previous->second.reference;
      SaveJson(directory/(reference+".json"),body);
      staged_replays_[request.view]={reference,uint32_t(request.record.size())};
      REXLOG_INFO("Community replay staged for native score: view={} bytes={}",request.view,request.record.size());
      return {};
    }
    body["score_id"]=found->second.at("server_id");
    auto directory=user_data_/"community"/"replay-outbox";
    LimitOutbox(directory,8);
    auto path=directory/(RandomReference()+".json");SaveJson(path,body);
    return {SendReplay(body,path,deadline),{}};
  }
  if(!request.record.empty())return {kInvalidData,{}};
  auto found=replay_references_.find({request.view,request.xuid});
  if(found==replay_references_.end() || found->second.replay.empty())return {ERROR_NOT_FOUND,{}};
  const auto reference=found->second;
  auto reply=Http("/replays/"+reference.replay,nullptr,deadline);
  if(auto error=ResultError(reply))return {error,{}};
  const auto& body=reply.body;
  if(body.at("protocol")!=1 || body.at("title_id")!="435A07D7" || body.at("id")!=reference.replay
      || body.at("score_id")!=reference.score || body.at("build")!=reference.build
      || body.at("mode")!=mode || body.at("format")!="futari-xstorage-v1")return {kInvalidData,{}};
  auto payload=Unbase64(body.at("payload").get<std::string>());
  if(body.at("size")!=payload.size() || body.at("sha256")!=Hash(payload))return {kInvalidData,{}};
  FutariValidateStorageReplay(payload);
  if(payload.size()>request.capacity)return {kInvalidData,{}};
  auto record=std::move(payload);
  REXLOG_INFO("Community replay verified: view={} bytes={}",request.view,record.size());
  return {0,std::move(record)};
}
uint32_t FutariCommunity::SendReplay(const nlohmann::json& body,const std::filesystem::path& path,Clock::time_point deadline) {
  if(!JournalMatches(body))return kInvalidData;
  auto request=body;request.erase("origin");
  auto reply=Http("/replays",std::move(request),deadline);if(auto error=ResultError(reply))return error;
  auto id=reply.body.at("id").get<std::string>();if(!IsReference(id))return kInvalidData;
  auto history=user_data_/"community"/"replay-history";std::filesystem::create_directories(history);
  // Keep the receipt, not another base64 copy of the recording, after success.
  auto receipt=body;receipt.erase("payload");receipt["id"]=id;
  SaveJson(history/path.filename(),receipt);
  std::filesystem::remove(path);
  REXLOG_INFO("Community replay accepted: bytes={} duplicate={}",body.at("payload").get_ref<const std::string&>().size(),reply.body.value("duplicate",false));
  return 0;
}
void FutariCommunity::ArchiveUnboundReplays() {
  const auto community=user_data_/"community";
  const auto pending=community/"pending-replays";
  if(!std::filesystem::exists(pending))return;
  const auto outbox=community/"outbox";
  const auto archive=community/"unbound-replays";
  for(const auto& directory:{community,pending,outbox,archive}) {
    if(std::filesystem::is_symlink(directory)
        || (std::filesystem::exists(directory) && !std::filesystem::is_directory(directory)))
      throw std::runtime_error("Invalid community journal directory");
  }
  // Read every durable association before moving anything. An unreadable or
  // malformed score journal is ambiguous, so retain all pending recordings.
  std::set<std::string> referenced;
  if(std::filesystem::exists(outbox))for(const auto& entry:std::filesystem::directory_iterator(outbox)) {
    if(entry.path().extension()!=".json" || !IsReference(entry.path().stem().string()))continue;
    if(entry.is_symlink() || !entry.is_regular_file() || entry.file_size()>1024*1024)
      throw std::runtime_error("Unreadable score journal");
    std::ifstream file(entry.path());auto batch=nlohmann::json::parse(file);file.close();
    if(!batch.is_object() || batch.value("version",0)!=2 || !JournalMatches(batch) || !batch.contains("scores")
        || !batch["scores"].is_array() || batch["scores"].size()>16)
      throw std::runtime_error("Invalid score journal");
    for(const auto& score:batch["scores"]) {
      if(!score.is_object())throw std::runtime_error("Invalid journal score");
      if(score.contains("pending_replay")) {
        auto reference=score.at("pending_replay").get<std::string>();
        if(!IsReference(reference))throw std::runtime_error("Invalid replay association");
        referenced.insert(std::move(reference));
      }
    }
  }
  size_t count=0;
  for(const auto& entry:std::filesystem::directory_iterator(pending)) {
    if(entry.is_symlink() || !entry.is_regular_file() || entry.path().extension()!=".json"
        || !IsReference(entry.path().stem().string()) || entry.file_size()>12*1024*1024
        || referenced.contains(entry.path().stem().string()))continue;
    std::ifstream file(entry.path());auto body=nlohmann::json::parse(file,nullptr,false);file.close();
    if(!body.is_object() || !JournalMatches(body) || body.value("protocol",0)!=1 || body.value("title_id","")!="435A07D7"
        || body.value("format","")!="futari-xstorage-v1" || body.contains("score_id"))continue;
    std::filesystem::create_directories(archive);
    auto target=archive/entry.path().filename();
    if(std::filesystem::exists(target))target=archive/(entry.path().stem().string()+"-"+RandomReference()+".json");
    std::filesystem::rename(entry.path(),target);
    ++count;
  }
  if(count)REXLOG_INFO("Community unbound recordings retained outside active queue: files={}",count);
}
void FutariCommunity::RetryOne() {
  auto directory=user_data_/"community"/"outbox";
  if(std::filesystem::is_directory(directory))for(const auto& entry:std::filesystem::directory_iterator(directory)) {
    if(!entry.is_regular_file()||entry.is_symlink()||entry.path().extension()!=".json"||!IsReference(entry.path().stem().string()))continue;
    if(entry.file_size()>1024*1024)continue;
    std::ifstream file(entry.path());auto batch=nlohmann::json::parse(file,nullptr,false);file.close();
    if(!batch.is_object()||batch.value("version",0)!=2||!JournalMatches(batch)||!batch.contains("scores")||!batch["scores"].is_array()||batch["scores"].size()>16)continue;
    SendBatch(batch,entry.path(),Clock::now()+std::chrono::seconds(8));return;
  }
  directory=user_data_/"community"/"replay-outbox";
  if(std::filesystem::is_directory(directory))for(const auto& entry:std::filesystem::directory_iterator(directory)) {
    if(!entry.is_regular_file()||entry.is_symlink()||entry.path().extension()!=".json"||!IsReference(entry.path().stem().string())
        || entry.file_size()>12*1024*1024)continue;
    std::ifstream file(entry.path());auto body=nlohmann::json::parse(file,nullptr,false);file.close();
    if(!body.is_object()||!JournalMatches(body)||body.value("protocol",0)!=1||body.value("title_id","")!="435A07D7"
        || !IsReference(body.value("score_id","")))continue;
    SendReplay(body,entry.path(),Clock::now()+std::chrono::seconds(8));return;
  }
}
