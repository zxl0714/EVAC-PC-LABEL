#include "community/sdoj_service.h"
#include "community/replay_format.h"
#include <rex/logging.h>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace sdoj;
using namespace rex::system::xam;
using namespace std::chrono_literals;
using cave::community::kNativeCommunityLocalXuid;
constexpr int64_t kTime=-5384438183559864396LL;
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F action) {
  try {action();}catch(const std::exception&){return;}
  throw std::runtime_error("Invalid SDOJ schema accepted");
}
template<class T,class F> T Complete(F action) {
  auto promise=std::make_shared<std::promise<T>>();auto future=promise->get_future();
  action([promise](T result){promise->set_value(std::move(result));});
  Require(future.wait_for(12s)==std::future_status::ready,"SDOJ request completion timed out");
  return future.get();
}
CommunityWrite Score(uint32_t view=23,int64_t value=12345) {
  return {view,{{0x20000004,2,value,0},{0x20000002,2,kTime,0},
      {0x10000007,1,ViewCharacterType(view),0},{0x10000006,1,0,0},
      {0x10000005,1,0,0},{0x10000001,1,0,0},{0x10008011,1,0,0}}};
}
CommunityWrite Storage(uint32_t size=336) {return {23,{{0x10008011,1,size,0}}};}
CommunityRead Board(uint32_t view=23){return {1,1,10,{},{{view,{1,2,4,5,6,0xfffa}}}};}
uint32_t Write(SdojCommunity& service,std::vector<CommunityWrite> views) {
  return Complete<uint32_t>([&](auto done){service.Write(std::move(views),done);});
}
CommunityResult Read(SdojCommunity& service,CommunityRead request) {
  return Complete<CommunityResult>([&](auto done){service.Read(std::move(request),done);});
}
CommunityRankResult Estimate(SdojCommunity& service,std::vector<CommunityRank> request) {
  return Complete<CommunityRankResult>([&](auto done){service.Estimate(std::move(request),done);});
}
CommunityReplayResult Replay(SdojCommunity& service,CommunityReplay request) {
  return Complete<CommunityReplayResult>([&](auto done){service.Replay(std::move(request),done);});
}
void Checksum(std::vector<uint8_t>& bytes) {
  bytes[8]=bytes[9]=0;uint32_t sum=0;
  for(size_t at=0;at<bytes.size();at+=2)sum+=(uint32_t(bytes[at])<<8|bytes[at+1])*uint32_t(at/2+1);
  bytes[8]=uint8_t(sum>>8);bytes[9]=uint8_t(sum);
}
std::vector<uint8_t> Recording(int64_t score=12345,uint32_t packed=48) {
  // Authored fixture, also constructed independently by the Python server tests.
  // Nonzero unused words are intentional. No ROM/replay content is embedded.
  std::vector<uint8_t> bytes(336,0xbe);
  auto put32=[&](size_t at,uint32_t value){for(int i=3;i>=0;--i){bytes[at+i]=uint8_t(value);value>>=8;}};
  auto put64=[&](size_t at,uint64_t value){put32(at,uint32_t(value>>32));put32(at+4,uint32_t(value));};
  put32(0,0x01304250);put64(24,uint64_t(score));put64(32,std::bit_cast<uint64_t>(kTime));
  put32(52,packed);put32(56,0);put32(60,0);put32(76,0);put32(92,uint32_t(bytes.size()));
  Checksum(bytes);return bytes;
}
int64_t Column(const CommunityRow& row,uint32_t id,uint32_t type) {
  for(const auto& column:row.columns)if(column.id==id){Require(column.type==type,"Native column type");return column.integer;}
  throw std::runtime_error("Native column missing");
}
void Units() {
  uint32_t boards=0;
  for(uint32_t view=0;view<100;++view) {
    if(ViewCharacterType(view)==UINT32_MAX)continue;
    ++boards;const auto body=EncodeCommunityScore(Score(view));
    Require(body.at("view")==view && body.at("columns")[2].at("value")==ViewCharacterType(view),"View/character association");
  }
  Require(boards==39 && ViewCharacterType(23)==48 && ViewCharacterType(24)==49 && ViewCharacterType(93)==50,"Native board table");
  for(auto view:{0u,1u,26u,44u,49u,84u,94u})Reject([&]{EncodeCommunityScore(Score(view));});
  auto encoded=EncodeCommunityScore(Score(23,9007199254740993LL));
  Require(encoded.at("score")=="9007199254740993" && encoded.at("columns")[0].at("id")==0x20000004,"SDOJ score ID/precision");
  Require(encoded.at("columns")[1].at("value")==std::to_string(kTime),"Signed native FILETIME changed");
  CommunityRow row;row.columns=DecodeCommunityColumns(encoded.at("columns"));
  Require(Column(row,2,2)==kTime && Column(row,4,2)==9007199254740993LL && Column(row,7,1)==48,"Read property namespace");
  Require(Column(row,0xfffa,1)==0,"Unconfirmed replay advertised");
  row.columns=DecodeCommunityColumns(encoded.at("columns"),336);
  Require(Column(row,0xfffa,1)==336 && NativeFiletime(kTime)==0x01dd5fb4b546a200ull,"Attachment/time interpretation");
  Reject([]{auto bad=Score();bad.columns[0].id=0x20000001;EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns[1].type=1;EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns[2].integer=32;EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns.pop_back();bad.columns.pop_back();EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns.push_back(bad.columns.front());EncodeCommunityScore(bad);});
  Reject([]{EncodeCommunityScore(Score(23,-1));});
  const auto bytes=Recording();const auto info=InspectReplay(bytes);
  Require(info.score==12345 && info.timestamp==kTime && info.character_type==48 && info.stage==0 && info.continues==0,"Native recording metadata");
  Require(cave::community::ReplayUnbase64(cave::community::ReplayBase64(bytes))==bytes,"Lossless native replay transport");
  for(auto offset:{0,8,24,52,92,320})Reject([&]{auto bad=bytes;bad[offset]^=1;InspectReplay(bad);});
  Reject([&]{auto bad=bytes;bad.pop_back();InspectReplay(bad);});
  Require(cave::community::NativeLegacyName("Long-ASCII-Player-Name")=="Long-ASCII-Play","Legacy ASCII name");
  Require(cave::community::NativeLegacyName("大佐").size()==15,"Legacy UTF-8 alias");
  SdojCommunity service("https://example.invalid/api","Player","authored-test");
  Require(Write(service,{Score(1)})==13 && Write(service,{Storage()})==13,"Invalid write reached HTTP");
  Require(Read(service,Board(44)).error==13 && Estimate(service,{{23,-1}}).error==13,"Invalid read/rank reached HTTP");
  SetCommunityService(std::shared_ptr<SdojCommunity>(&service,[](auto*){}));
  auto path=CommunityReplayPath(23,kNativeCommunityLocalXuid);uint32_t view=0;uint64_t owner=0;
  Require(path.starts_with(u"sdoj-replay/") && ParseCommunityReplayPath(path,view,owner) && view==23 && owner==kNativeCommunityLocalXuid,"SDOJ replay namespace");
  Require(!ParseCommunityReplayPath(u"espgaluda2-replay/00000017/b13ebabebabebabe",view,owner),"Cross-game recording namespace");
  SetCommunityService({});service.Stop();service.Stop();
  Require(Read(service,Board()).error==995,"Stopped SDOJ service accepted I/O");
}
nlohmann::json Probe(std::string endpoint) {
  Require(endpoint.starts_with("http://127.0.0.1:"),"Authored probe requires isolated loopback service");
  SdojCommunity player(endpoint,"SdojTransportTest","authored-test"),rival(endpoint,"SdojRival","authored-test");
  auto own=Board();own.xuids={kNativeCommunityLocalXuid};
  auto unranked=Read(player,own);
  Require(!unranked.error && unranked.views[0].total==0 && unranked.views[0].rows.size()==1 && unranked.views[0].rows[0].rank==0 && Column(unranked.views[0].rows[0],2,2)==0,"Unranked native local row");
  Require(Read(player,Board()).views[0].rows.empty(),"Unranked row leaked to public board");
  Require(Write(player,{Storage()})==13,"Unaccepted storage acknowledgment");
  const auto record=Recording();
  Require(Replay(player,{23,kNativeCommunityLocalXuid,true,336,record}).error==13,"Recording uploaded before score");
  Require(Write(player,{Score()})==0,"Native SDOJ score upload");
  auto score_only=Read(player,Board());
  Require(!score_only.error && Column(score_only.views[0].rows[0],0xfffa,1)==0,"Score size flag mistaken for attachment");
  auto wrong=Recording(999);
  Require(Replay(player,{23,kNativeCommunityLocalXuid,true,336,wrong}).error==13,"Wrong native run attached");
  Require(!Replay(player,{23,kNativeCommunityLocalXuid,true,336,record}).error,"Native SDOJ replay upload");
  Require(Write(player,{Storage()})==0 && Write(player,{Storage()})==0,"Native storage-size-only acknowledgment");
  auto board=Read(player,Board());const auto& row=board.views[0].rows[0];
  Require(!board.error && row.rating==12345 && row.xuid==kNativeCommunityLocalXuid && Column(row,2,2)==kTime && Column(row,4,2)==12345 && Column(row,0xfffa,1)==336,"Native SDOJ readback");
  auto downloaded=Replay(player,{23,kNativeCommunityLocalXuid,false,336,{}});
  Require(!downloaded.error && downloaded.record==record,"Native SDOJ replay byte equality");
  Require(Replay(player,{23,kNativeCommunityLocalXuid,false,334,{}}).error==122,"SDOJ download capacity");
  auto foreign=Read(rival,Board());const auto other=foreign.views[0].rows[0].xuid;
  Require(other!=kNativeCommunityLocalXuid && rival.DisplayName(other)=="SdojTransportTest","Remote native nickname identity");
  Require(Replay(rival,{23,other,false,336,{}}).record==record,"Remote SDOJ replay download");
  constexpr int64_t score=9007199254740993LL;
  Require(Write(player,{Score(24,score)})==0 && Write(rival,{Score(24,score+1)})==0,"SDOJ Laser scores");
  auto estimate=Estimate(player,{{24,score+2},{24,score+1}});
  Require(!estimate.error && estimate.ranks==std::vector<uint32_t>{1,2},"SDOJ rank estimate");
  auto laser=Read(player,Board(24));
  Require(!laser.error && laser.views[0].total==2 && laser.views[0].rows[1].rating==score && Column(laser.views[0].rows[1],7,1)==49,"SDOJ Laser association/precision");
  Require(Write(player,{Storage()})==13,"Previous-run replay receipt reused by new run");
  player.Stop();rival.Stop();
  return {{"passed",true},{"guestExecution",false},{"game","sdoj"},{"replayBytes",336},{"nativeSizeAcknowledgment",true}};
}
int main(int argc,char** argv) {
  try {
    rex::InitLoggingEarly();auto config=rex::BuildLogConfig(nullptr,"error",{});rex::InitLogging(config);
    if(argc==1){Units();std::cout<<"SDOJ schema/worker checks passed; no connections made\n";}
    else if(argc==3 && std::string_view(argv[1])=="--probe")std::cout<<Probe(argv[2]).dump()<<'\n';
    else if(argc==3 && std::string_view(argv[1])=="--inspect-replay") {
      std::ifstream input(argv[2],std::ios::binary);Require(bool(input),"Cannot open recording");
      std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input),{});auto info=InspectReplay(bytes);
      std::cout<<nlohmann::json({{"bytes",bytes.size()},{"score",std::to_string(info.score)},
          {"timestamp",std::to_string(info.timestamp)},{"filetime",std::to_string(NativeFiletime(info.timestamp))},
          {"characterType",info.character_type},{"stage",info.stage},{"continues",info.continues},
          {"sha256",cave::community::ReplaySha256(bytes)}}).dump()<<'\n';
    }else throw std::runtime_error("Usage: sdoj_community_tests [--probe <loopback API root> | --inspect-replay <file>]");
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
