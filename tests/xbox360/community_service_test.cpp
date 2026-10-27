#include "community/g2_service.h"
#include "community/replay_format.h"
#include "community/path_buffer.h"
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/logging.h>
#include <chrono>
#include <array>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <filesystem>

using namespace espgaluda2;
using namespace rex::system::xam;
using namespace std::chrono_literals;
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class T, class F> T Complete(F action) {
  auto promise = std::make_shared<std::promise<T>>();
  auto future = promise->get_future();
  action([promise](T result) { promise->set_value(std::move(result)); });
  Require(future.wait_for(12s) == std::future_status::ready, "Community completion timed out");
  return future.get();
}
CommunityWrite Score(uint32_t view, int64_t score) {
  return {view, {{0x20000001,2,score,0}, {0x20000002,2,9007199254740001LL,0},
                 {0x20000003,2,1234,0}, {0x10000008,1,2,0}, {0x10008011,1,4096,0},
                 {0x20000011,2,134368194753020000LL,0}}};
}
CommunityRead Board(uint32_t view) { return {1,1,20,{},{{view,{1,2,7,8,0xfffa}}}}; }
uint32_t Write(G2Community& service, std::vector<CommunityWrite> views) {
  return Complete<uint32_t>([&](auto done) { service.Write(std::move(views), done); });
}
CommunityResult Read(G2Community& service, CommunityRead request) {
  return Complete<CommunityResult>([&](auto done) { service.Read(std::move(request), done); });
}
CommunityRankResult Estimate(G2Community& service, std::vector<CommunityRank> request) {
  return Complete<CommunityRankResult>([&](auto done) { service.Estimate(std::move(request), done); });
}
CommunityReplayResult Replay(G2Community& service,CommunityReplay request) {
  return Complete<CommunityReplayResult>([&](auto done){service.Replay(std::move(request),done);});
}
std::vector<uint8_t> ReplayFixture(int64_t score=999) {
  std::vector<uint8_t> bytes(136);
  auto put32=[&](size_t at,uint32_t n) {for(int i=3;i>=0;--i){bytes[at+i]=uint8_t(n);n>>=8;}};
  auto put64=[&](size_t at,uint64_t n){put32(at,uint32_t(n>>32));put32(at+4,uint32_t(n));};
  put32(0,0);put32(4,2);put64(8,134368194753020000ull);put64(16,uint64_t(score));
  put32(24,12);put32(36,2);put32(96,32);
  put64(64,kCommunityLocalXuid);
  constexpr std::string_view tag="OldTag";
  std::copy(tag.begin(),tag.end(),bytes.begin()+80);
  for(size_t i=104;i<bytes.size();++i)bytes[i]=uint8_t(i);
  return bytes;
}
CommunityWrite ReplayScore(uint32_t view,int64_t score=999) {
  auto value=Score(view,score);value.columns[4].integer=136;return value;
}
template<class F> void Reject(F action) {
  try { action(); } catch (const std::runtime_error&) { return; }
  throw std::runtime_error("Invalid native schema accepted");
}

std::string ReplayTag(const std::vector<uint8_t>& bytes) {
  const auto start=bytes.begin()+80,end=bytes.begin()+96;
  return {start,std::find(start,end,0)};
}
// Mirrors the original list refresh's three-way owner choice. Actual game
// acceptance is recorded separately; this is not guest execution.
std::string ListAuthor(const std::vector<uint8_t>& bytes,std::string local_name) {
  uint64_t xuid=0;for(size_t i=64;i<72;++i)xuid=(xuid<<8)|bytes[i];
  if(!xuid)return ReplayTag(bytes);
  return xuid==kCommunityLocalXuid?local_name:"Live profile lookup";
}
void CheckDownload(const std::vector<uint8_t>& original,const std::vector<uint8_t>& downloaded,
                   std::string_view author) {
  Require(downloaded.size()==original.size(),"Replay size changed");
  for(size_t i=4;i<original.size();++i)
    if(!(i>=64 && i<72) && !(i>=80 && i<96))
      Require(downloaded[i]==original[i],"Non-author replay data changed");
  Require(ReplayTag(downloaded)==author && ListAuthor(downloaded,"RenamedViewer")==author,
          "Archive author changed on local nickname update");
  Require(std::all_of(downloaded.begin()+64,downloaded.begin()+72,[](auto b){return b==0;}),
          "Archive still resolves a local or remote Live profile");
  uint32_t sum=uint32_t(downloaded.size()),saved=0;
  for(size_t i=4;i<downloaded.size();++i)sum+=downloaded[i];
  for(size_t i=0;i<4;++i)saved=(saved<<8)|downloaded[i];
  Require(sum==saved,"Native replay checksum was not refreshed");
}

void DownloadPathBufferTest() {
  const auto directory=std::filesystem::temp_directory_path()/
      ("evac-g2-path-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);} } cleanup{directory};
  rex::Runtime runtime(directory,directory,{},directory/"cache",{});
  rex::RuntimeConfig config;config.tool_mode=true;
  Require(runtime.Setup(std::move(config))==0,"Download path runtime setup failed");
  auto provider=std::make_shared<G2Community>("https://example.invalid/api","PathTest","test");
  SetCommunityService(provider);
  struct Reset { ~Reset(){SetCommunityService({});} } reset;
  auto* memory=runtime.memory();
  const auto address=memory->SystemHeapAlloc(4096);
  Require(address!=0,"Download path fixture allocation failed");
  const auto destination=address+4,length=destination+2048;
  auto* bytes=memory->TranslateVirtual(address);
  auto put=[&](uint32_t at,uint32_t value){rex::memory::store_and_swap<uint32_t>(bytes+at,value);};
  auto get=[&](uint32_t at){return rex::memory::load_and_swap<uint32_t>(bytes+at);};
  put(0,73);put(2052,0);std::fill(bytes+4,bytes+2052,0xcc);
  const auto build=[&]{return BuildCommunityReplayPath(runtime.kernel_state(),254,
      0xb000000100000002ull,1,address,4,0,destination,length);};
  Require(build()==122 && bytes[4]==0xcc,"Shared API accepted zero capacity");
  put(2052,0); // Reproduce the actual cold download, not the second-attempt state.
  Require(InitializeDownloadPathCapacity(*memory,address,destination,length),"Cold G2 path capacity rejected");
  Require(get(2052)==2048 && build()==0,"First G2 download path failed");
  const auto path=CommunityReplayPath(73,0xb000000100000002ull);
  const auto required=uint32_t((path.size()+1)*2);
  Require(get(2052)==required && bytes[4+required]==0xcc,"Download path output bounds changed");
  for(size_t i=0;i<path.size();++i)
    Require(rex::memory::load_and_swap<uint16_t>(bytes+4+i*2)==path[i],"Download path contents wrong");
  Require(!rex::memory::load_and_swap<uint16_t>(bytes+4+path.size()*2),"Download path lacks terminator");
  put(0,39);
  Require(InitializeDownloadPathCapacity(*memory,address,destination,length) && build()==0,
      "Repeated download did not restore array capacity");
  const auto unchanged=get(2052);
  Require(!InitializeDownloadPathCapacity(*memory,address,destination,length-4) && get(2052)==unchanged,
      "Unknown layout altered capacity");
  Require(!InitializeDownloadPathCapacity(*memory,address+4,destination,length),"Wrong view layout accepted");
  Require(!InitializeDownloadPathCapacity(*memory,1,5,2053),"Unmapped buffer accepted");
  Require(!InitializeDownloadPathCapacity(*memory,0xfffffffc,0,2048),"Wrapped buffer accepted");
  memory->SystemHeapFree(address);
}
void Units() {
  DownloadPathBufferTest();
  auto encoded = EncodeCommunityScore(Score(71,9007199254740993LL));
  Require(encoded.at("score") == "9007199254740993", "64-bit total rounded");
  Require(encoded.at("columns").at(0).at("id") == 0x20000001, "Native write property namespace lost");
  auto decoded = DecodeCommunityColumns(encoded.at("columns"));
  Require(decoded.size() == 5 && decoded[0].id == 1 && decoded[0].integer == 9007199254740001LL,
          "G2 stage view column mapping");
  Require(decoded.back().id == 0xfffa && decoded.back().integer == 0, "Unverified replay advertised");
  Require(decoded[2].id == 7 && decoded[2].integer == 2 && decoded[2].type == 1,
          "Seseri upload property must populate character read column 7");
  auto metadata=Score(1,100);
  metadata.columns={{0x10000008,1,2,0},{0x1000000a,1,21,0},{0x1000000b,1,22,0},
      {0x10000009,1,23,0},{0x1000000f,1,24,0},{0x1000000c,1,25,0},
      {0x10000010,1,26,0},{0x1000000d,1,0x43415600,0},
      {0x20000011,2,134368194753020000LL,0},{0x20000001,2,100,0}};
  auto all=DecodeCommunityColumns(EncodeCommunityScore(metadata).at("columns"));
  Require(all.size()==10, "Complete metadata column count");
  const std::array<int64_t,8> expected_metadata{2,25,26,21,24,23,22,0x43415600};
  for(size_t i=0;i<8;++i) Require(all[i].id==7+i && all[i].type==1 &&
      all[i].integer==expected_metadata[i], "Metadata property/read column order");
  auto stage_two=Score(24,7614133);
  stage_two.columns.push_back({0x1000000c,1,1,0});
  stage_two.columns.push_back({0x10000009,1,0,0});
  auto stage_columns=DecodeCommunityColumns(EncodeCommunityScore(stage_two).at("columns"));
  auto column_value=[&](uint32_t id) {for(const auto& col:stage_columns)if(col.id==id)return col.integer;
    throw std::runtime_error("Required metadata read column missing");};
  Require(column_value(8)==1 && column_value(12)==0,"Stage 2 must not be displayed as Stage 1 or Continue 1");
  Require(all[8].id==15 && all[8].type==2 && all[8].integer==134368194753020000LL,
          "Native UTC FILETIME rounded or shifted during board decode");
  Reject([] { auto v=Score(1,100); v.columns[3].type=2; DecodeCommunityColumns(EncodeCommunityScore(v).at("columns")); });
  Reject([] { EncodeCommunityScore(Score(0,10)); });
  Reject([] { EncodeCommunityScore(Score(85,10)); });
  Reject([] { EncodeCommunityScore(Score(1,-1)); });
  Reject([] { auto v=Score(1,10); v.columns.push_back(v.columns.front()); EncodeCommunityScore(v); });
  Reject([] { auto v=Score(1,10); v.columns.front().type=1; EncodeCommunityScore(v); });
  Reject([] { DecodeCommunityColumns(nlohmann::json::array({{{"id",0x20000002},{"type",2},{"value","9223372036854775808"}}})); });
  Require(CommunityNativeName("An-ASCII-Nickname-Too-Long") == "An-ASCII-Nickna", "Native ASCII tag limit");
  const auto alias = CommunityNativeName("大佐");
  Require(alias.starts_with("Player-") && alias.size() == 15 && alias == CommunityNativeName("大佐"), "Stable display alias");
  auto fixture=ReplayFixture();
  Require(ListAuthor(fixture,"Viewer")=="Viewer","Fixture does not reproduce local rename");
  auto archived=fixture;
  SetDownloadedReplayAuthor(archived,"RemoteAuthor");
  CheckDownload(fixture,archived,"RemoteAuthor");
  const auto once=archived;
  SetDownloadedReplayAuthor(archived,"RemoteAuthor");
  Require(archived==once && ReplayTag(fixture)=="OldTag","Author preparation changed original or is not idempotent");
  for(const auto& invalid:{std::string(),std::string(16,'x'),std::string("A\nB"),std::string("\xc3\xa9")}) {
    auto bad=fixture;Reject([&]{SetDownloadedReplayAuthor(bad,invalid);});
    Require(bad==fixture,"Invalid tag partially rewrote metadata");
  }
  auto invalid_record=fixture;invalid_record[7]=3;const auto before_invalid=invalid_record;
  Reject([&]{SetDownloadedReplayAuthor(invalid_record,"Author");});
  Require(invalid_record==before_invalid,"Invalid record was modified");
  auto info=InspectReplay(fixture);
  Require(info.score==999 && info.time==134368194753020000ull && info.character==2,"Replay envelope metadata");
  auto local_prefix=fixture;
  for(size_t i=0;i<4;++i)local_prefix[i]=0xff;
  Require(InspectReplay(local_prefix).score==999,"Local checksum mistaken for a network size");
  Require(ReplayUnbase64(ReplayBase64(fixture))==fixture && ReplaySha256(fixture).size()==64,"Replay bytes/encoding");
  Reject([&]{auto bad=fixture;bad.resize(104);InspectReplay(bad);});
  Reject([&]{auto bad=fixture;bad[7]=3;InspectReplay(bad);});
  Reject([&]{auto bad=fixture;bad[99]=33;InspectReplay(bad);});
  Reject([&]{auto bad=fixture;bad[39]=3;InspectReplay(bad);});
  Reject([]{ReplayUnbase64("YQ==\n");});
  Reject([]{ReplayUnbase64("YR==");});
  Reject([] { G2Community invalid("http://example.invalid/api", "Player", "test"); });
  Reject([] { G2Community invalid("https://example.invalid/api", "\n", "test"); });
  // No requests reach HTTP: invalid schema is rejected by the service worker.
  G2Community service("https://example.invalid/api", "Player", "test");
  Require(Write(service,{Score(0,10)}) == 13, "Invalid native view reached HTTP");
  auto invalid=Board(85);
  Require(Read(service,invalid).error == 13, "Invalid native board reached HTTP");
  Require(Estimate(service,{{1,-1}}).error == 13, "Invalid estimate reached HTTP");
  SetCommunityService(std::shared_ptr<G2Community>(&service,[](auto*){}));
  const auto path=CommunityReplayPath(36,kCommunityLocalXuid);
  uint32_t parsed_view=0;uint64_t parsed_owner=0;
  Require(path.starts_with(u"espgaluda2-replay/") && ParseCommunityReplayPath(path,parsed_view,parsed_owner) &&
      parsed_view==36 && parsed_owner==kCommunityLocalXuid,"G2 replay namespace/round trip");
  Require(!ParseCommunityReplayPath(u"futari-replay/00000024/b13ebabebabebabe",parsed_view,parsed_owner),"Cross-title replay path accepted");
  SetCommunityService({});
  Require(CommunityReplayPath(36,kCommunityLocalXuid).starts_with(u"futari-replay/"),"Existing default namespace changed");
  service.Stop(); service.Stop();
  Require(Read(service,Board(1)).error == 995, "Stopped service accepted IO");
  Require(service.DisplayName(kCommunityLocalXuid) == "Player", "Local ABI token changed identity");
  CommunityResult unranked{0,{{71,0,{{kCommunityLocalXuid,0,0,"Player",{{1,2,0,0}},"Player"}}}}};
  CommunitySpec spec{71,{1,0xfffa}};
  std::vector<uint8_t> bytes(CommunityStatsBufferSize({&spec,1},1));
  Require(SerializeCommunityStats(unranked,{&spec,1},0x1000,bytes), "Unranked native stats serialization");
  auto be32=[&](size_t at) {return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];};
  Require(be32(8+4)==0 && be32(8+8)==1 && be32(8+12)>=0x1000+24,
          "Explicit unranked stats must contain an actual row pointer, not a public ranked score");
}

nlohmann::json CheckEndpoint(std::string endpoint) {
  Require(endpoint.starts_with("https://"), "Public endpoint checks require HTTPS");
  G2Community service(endpoint,"CollectionCheck","endpoint-read-only");
  auto public_board=Read(service,Board(71));
  Require(!public_board.error && public_board.views.size()==1, "HTTPS native leaderboard read failed");
  auto own=Board(71);own.xuids={kCommunityLocalXuid};
  auto personal=Read(service,own);
  Require(!personal.error && personal.views.size()==1 && personal.views[0].rows.size()==1,
          "HTTPS native personal lookup failed");
  service.Stop();
  return {{"passed",true},{"readOnly",true},{"guestExecution",false},
          {"view",71},{"players",public_board.views[0].total}};
}

nlohmann::json Probe(std::string endpoint) {
  Require(endpoint.starts_with("http://127.0.0.1:"), "Probe requires isolated loopback service");
  constexpr int64_t score=9007199254740993LL;
  G2Community player(endpoint,"G2TransportTest","authored-test");
  G2Community rival(endpoint,"G2Rival","authored-test");
  auto own=Board(71);own.xuids={kCommunityLocalXuid};
  auto empty=Read(player,own);
  Require(!empty.error && empty.views.size()==1 && empty.views[0].total==0 && empty.views[0].rows.size()==1 &&
      empty.views[0].rows[0].rank==0 && empty.views[0].rows[0].rating==0,
      "Explicit unranked user stats require a local zero-valued row");
  auto empty_board=Read(player,Board(71));
  Require(!empty_board.error && empty_board.views[0].rows.empty() && empty_board.views[0].total==0,
          "Local unranked stats leaked into the public board");
  auto first_rank=Estimate(player,{{71,score},{72,score}});
  Require(!first_rank.error && first_rank.ranks==std::vector<uint32_t>{1,1}, "Empty board estimate");
  Require(Write(player,{Score(71,score),Score(72,score)})==0, "Native batch upload failed");
  Require(Write(rival,{Score(71,score+1)})==0, "Rival upload failed");
  auto board=Read(player,Board(71));
  Require(!board.error && board.views.size()==1 && board.views[0].total==2 && board.views[0].rows.size()==2, "Native board read failed");
  const auto& rows=board.views[0].rows;
  Require(rows[0].nickname=="G2Rival" && rows[0].rank==1 && rows[0].rating==score+1, "Ranking order");
  Require(rows[1].xuid==kCommunityLocalXuid && rows[1].rank==2 && rows[1].rating==score, "Local nickname ABI identity");
  Require(player.DisplayName(rows[0].xuid)=="G2Rival", "Remote nickname cache");
  Require(rows[1].columns[0].id==1 && rows[1].columns[0].integer==9007199254740001LL, "Stage precision");
  Require(rows[1].columns[2].id==7 && rows[1].columns[2].integer==2 &&
          rows[1].columns[3].id==15 && rows[1].columns[3].integer==134368194753020000LL,
          "Character and UTC time metadata did not round-trip");
  auto local=Read(player,own);
  Require(!local.error && local.views[0].rows.size()==1 && local.views[0].rows[0].rank==2, "Explicit user lookup");
  auto around=Board(71);around.type=0;around.pivot=kCommunityLocalXuid;around.count=1;
  auto centered=Read(player,around);
  Require(!centered.error && centered.views[0].rows.size()==1 && centered.views[0].rows[0].nickname=="G2TransportTest", "Around-user lookup");
  auto estimates=Estimate(player,{{71,score+2},{71,score+1}});
  Require(!estimates.error && estimates.ranks==std::vector<uint32_t>{1,2}, "Rank estimate exclusion/ties");
  Require(Write(player,{Score(71,100)})==0, "Lower authored score rejected");
  auto retained=Read(player,own);
  Require(!retained.error && retained.views[0].rows[0].rating==score, "Lower submission replaced a personal best");
  const auto record=ReplayFixture();
  Require(Replay(player,{1,kCommunityLocalXuid,true,136,record}).error==13,"Replay uploaded without an accepted score");
  // The title submits at most five views per batch. A second batch must retain
  // receipts from the same run, without carrying an older run's receipts over.
  Require(Write(player,{ReplayScore(1)})==0 && Write(player,{ReplayScore(2)})==0,"Replay score batches");
  auto unshared=Read(player,Board(1));
  Require(unshared.views[0].rows[0].columns.back().integer==0,"Unshared upload size advertised as downloadable");
  auto wrong=record;wrong[23]^=1;
  Require(Replay(player,{1,kCommunityLocalXuid,true,136,wrong}).error==13,"Wrong-run replay attached");
  Require(!Replay(player,{1,kCommunityLocalXuid,true,136,record}).error &&
      !Replay(player,{2,kCommunityLocalXuid,true,136,record}).error,"Score-before-replay upload failed");
  Require(!Replay(player,{1,kCommunityLocalXuid,true,136,record}).error,"Replay upload retry not idempotent");
  auto shared=Read(player,Board(1));
  Require(!shared.error && shared.views[0].rows[0].columns.back().integer==136,"Native attachment byte count missing");
  auto download=Replay(player,{1,kCommunityLocalXuid,false,136,{}});
  Require(!download.error,"Own replay download failed");
  const auto expected_author=CommunityNativeName("G2TransportTest");
  CheckDownload(record,download.record,expected_author);
  Require(Replay(player,{1,kCommunityLocalXuid,false,135,{}}).error==122,"Download overflow not rejected");
  auto foreign=Read(rival,Board(1));
  const auto owner=foreign.views[0].rows[0].xuid;
  auto remote=Replay(rival,{1,owner,false,136,{}});
  Require(!remote.error && remote.record==download.record,"Remote nickname replay lookup failed");
  CheckDownload(record,remote.record,expected_author);
  G2Community cold(endpoint,"RenamedViewer","authored-test");
  Read(cold,Board(71)); // Allocate different temporary leaderboard IDs first.
  const auto cold_board=Read(cold,Board(1));
  const auto cold_download=Replay(cold,{1,cold_board.views.at(0).rows.at(0).xuid,false,136,{}});
  Require(!cold_download.error && cold_download.record==download.record,"Cold profile changed archived author");
  cold.Stop();
  // This round-trip uses only saved bytes; no network call supplies the name.
  const auto fixture_path=std::filesystem::temp_directory_path()/
      ("evac-g2-author-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
  {std::ofstream file(fixture_path,std::ios::binary);file.write(reinterpret_cast<const char*>(download.record.data()),download.record.size());Require(bool(file),"Offline fixture write failed");}
  std::vector<uint8_t> restored;
  {std::ifstream file(fixture_path,std::ios::binary);restored.assign(std::istreambuf_iterator<char>(file),{});}
  std::filesystem::remove(fixture_path);
  Require(restored==download.record && ListAuthor(restored,"OfflineUser")==expected_author,"Offline archived author changed");
  Require(Write(player,{Score(0,10)})==13 && Replay(player,{1,kCommunityLocalXuid,true,136,record}).error==13,
          "Failed new write retained an old upload receipt");
  player.Stop();rival.Stop();
  Require(Read(player,Board(71)).error==995, "Stopped service completion");
  return {{"passed",true},{"nativeGuestExecution",false},{"score",std::to_string(score)},
          {"rows",2},{"rank",2},{"replaySupported",true}};
}

int main(int argc,char** argv) {
  try {
    rex::InitLoggingEarly();auto log=rex::BuildLogConfig(nullptr,"error",{});rex::InitLogging(log);
    if(argc==1) { Units();std::cout<<"G2 community schema/worker tests passed; no connections made\n"; }
    else if(argc==3 && std::string_view(argv[1])=="--probe") std::cout<<Probe(argv[2]).dump()<<'\n';
    else if(argc==3 && std::string_view(argv[1])=="--check-endpoint") std::cout<<CheckEndpoint(argv[2]).dump()<<'\n';
    else if(argc==3 && std::string_view(argv[1])=="--inspect-replay") {
      std::ifstream input(argv[2],std::ios::binary);Require(bool(input),"Replay file unavailable");
      const auto bytes=std::vector<uint8_t>(std::istreambuf_iterator<char>(input),{});
      const auto info=InspectReplay(bytes);
      std::cout<<nlohmann::json({{"bytes",bytes.size()},{"time",std::to_string(info.time)},
          {"score",std::to_string(info.score)},{"character",info.character},{"mode",info.mode},
          {"sha256",ReplaySha256(bytes)}}).dump()<<'\n';
    }
    else throw std::runtime_error("Usage: community_service_tests [--probe <loopback API root> | --check-endpoint <HTTPS API root> | --inspect-replay <file>]");
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 2; }
}
