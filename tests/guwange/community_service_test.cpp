#include "community/guwange_service.h"
#include "community/replay_format.h"
#include <rex/logging.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace guwange;
using namespace rex::system::xam;
using namespace std::chrono_literals;
using cave::community::kNativeCommunityLocalXuid;
namespace {
int checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F action) {
  ++checks;try{action();}catch(const std::exception&){return;}
  throw std::runtime_error("Invalid Guwange schema accepted");
}
template<class T,class F> T Complete(F action) {
  auto promise=std::make_shared<std::promise<T>>();auto future=promise->get_future();
  action([promise](T result){promise->set_value(std::move(result));});
  Require(future.wait_for(12s)==std::future_status::ready,"Native request timeout");return future.get();
}
CommunityWrite Score(uint32_t view=1,uint32_t character=0,int64_t score=12345) {
  return {view,{{0x20000004,2,score,0},{0x10000008,1,character,0},{0x10000006,1,0,0},
      {0x10000001,1,0,0},{0x10000003,1,0,0},
      {0x20000002,2,std::bit_cast<int64_t>(0x9b50160001dd63a6ULL),0},
      {0x10000005,1,0,0},{0x10008011,1,0,0}}};
}
std::vector<uint8_t> Recording(uint32_t mode=0,uint32_t character=0,uint32_t score=12345) {
  std::vector<uint8_t> data(kReplayHeaderBytes+kReplayFixedPayloadBytes+67,0xa5);
  constexpr std::string_view names[]{"GWN_for_XB360_AC20100820","GWN_for_XB360_BL20100820","GWN_for_XB360_XB20100820"};
  std::copy(names[mode].begin(),names[mode].end(),data.begin());data[names[mode].size()]=0;
  auto put32=[&](size_t at,uint32_t value){for(int i=3;i>=0;--i){data[at+i]=uint8_t(value);value>>=8;}};
  put32(36,uint32_t(data.size()-256));put32(40,0x01dd63a6);put32(44,0x1a501600);
  put32(48,2);put32(52,mode);put32(56,1);put32(60,0);
  put32(76,character);put32(80,score);put32(104,UINT32_MAX);put32(108,0);
  int64_t sum=0;for(size_t i=256;i<data.size();++i)sum+=data[i]>=128?int(data[i])-256:data[i];
  data[32]=uint8_t(uint16_t(sum)>>8);data[33]=uint8_t(sum);return data;
}
int64_t Column(const CommunityRow& row,uint32_t id) {
  for(const auto& col:row.columns)if(col.id==id){Require(col.type==1,"Column type");return col.integer;}
  throw std::runtime_error("Native read column missing");
}
uint32_t Write(GuwangeCommunity& service,std::vector<CommunityWrite> views) {
  return Complete<uint32_t>([&](auto done){service.Write(std::move(views),done);});
}
CommunityRead Board(uint32_t view){return {1,1,10,{},{{view,{uint16_t(view==1?4:6),1,2,3,uint16_t(view==1?5:4),7,0xfffa}}}};}
CommunityRead PersonalBest(uint32_t combined,uint32_t personal) {
  auto request=Board(combined);request.type=0;request.pivot=kNativeCommunityLocalXuid;request.count=1;
  request.specs.push_back(Board(personal).specs[0]);return request;
}
void CheckPersonalBest(const CommunityResult& result,const CommunityRead& request,int64_t score,uint32_t total) {
  Require(!result.error && result.views.size()==2,"Native personal-best result");
  for(const auto& view:result.views) {
    Require(view.total==total && view.rows.size()==1 && view.rows[0].xuid==kNativeCommunityLocalXuid &&
        view.rows[0].rating==score && (score?view.rows[0].rank>0:view.rows[0].rank==0),
        "Native personal-best lookup must return the requested user's row, including an unranked user");
  }
  // 821D5968 reads first-row rating without checking cRows. Exercise the actual
  // SDK serializer so an empty pRows cannot reach that original instruction.
  constexpr uint32_t base=0x40001000;
  std::vector<uint8_t> bytes(CommunityStatsBufferSize(request.specs,1));
  Require(SerializeCommunityStats(result,request.specs,base,bytes),"Native personal-best serialization");
  auto be32=[&](size_t at){return uint32_t(bytes.at(at))<<24|uint32_t(bytes.at(at+1))<<16|
      uint32_t(bytes.at(at+2))<<8|bytes.at(at+3);};
  for(size_t i=0;i<2;++i) {
    const auto view=8+i*16;const auto row=be32(view+12);
    Require(be32(view+8)==1 && row>=base && size_t(row-base)+48<=bytes.size(),"Native first-row pointer");
    Require(be32(row-base+16)==uint64_t(score)>>32 && be32(row-base+20)==uint32_t(score),"Native first-row rating");
  }
}
CommunityResult Read(GuwangeCommunity& service,CommunityRead request) {
  return Complete<CommunityResult>([&](auto done){service.Read(std::move(request),done);});
}
CommunityReplayResult Replay(GuwangeCommunity& service,CommunityReplay request) {
  return Complete<CommunityReplayResult>([&](auto done){service.Replay(std::move(request),done);});
}
void Units() {
  // Native 821BF1EC..821BF1FC packs the FILETIME words low-first. The
  // signed XUSER_DATA value may be negative; keep its exact wire bits.
  for(auto packed:{0x1b50160001dd63a6ULL,0x9b50160001dd63a6ULL,0xb663f16001dd63b0ULL}) {
    auto score=Score(1,0,2780);score.columns[5].integer=std::bit_cast<int64_t>(uint64_t(packed));
    const auto encoded=EncodeCommunityScore(score);
    Require(encoded.at("columns")[5].at("value")==std::to_string(score.columns[5].integer),
        "Native packed time changed during encoding");
    Require(DecodeCommunityColumns(1,encoded.at("columns")).size()==3,"Packed time readback");
    MatchCommunityReplay(1,encoded.at("columns"),Recording(0,0,2780));++checks;
  }
  for(auto packed:{int64_t(0),int64_t(0x80000000)})
    Reject([&]{auto bad=Score();bad.columns[5].integer=packed;EncodeCommunityScore(bad);});
  for(uint32_t view=1;view<=12;++view) {
    const auto slot=(view-1)%4,character=slot?slot-1:2;
    const auto body=EncodeCommunityScore(Score(view,character));
    Require(body.at("score")=="12345" && body.at("view")==view,"Native score encoding");
    CommunityRow row;row.columns=DecodeCommunityColumns(view,body.at("columns"));
    Require(row.columns.size()==3 && Column(row,view==1?4:6)==character &&
        Column(row,view==1?5:4)==0 && Column(row,0xfffa)==0,"View-aware native columns");
    auto record=Recording((view-1)/4,character);MatchCommunityReplay(view,body.at("columns"),record);
    ++checks;
    row.columns=DecodeCommunityColumns(view,body.at("columns"),uint32_t(record.size()));
    Require(Column(row,0xfffa)==int64_t(record.size()),"Confirmed replay size");
    for(uint32_t other=0;other<3;++other)if(other!=(view-1)/4)
      Reject([&]{MatchCommunityReplay(view,body.at("columns"),Recording(other,character));});
    Reject([&]{MatchCommunityReplay(view,body.at("columns"),Recording((view-1)/4,character,999));});
  }
  for(auto view:{0u,13u,99u})Reject([&]{EncodeCommunityScore(Score(view));});
  Reject([]{EncodeCommunityScore(Score(2,1));});
  Reject([]{EncodeCommunityScore(Score(1,0,-1));});
  Reject([]{EncodeCommunityScore(Score(1,0,0x80000000LL));});
  Reject([]{auto bad=Score();bad.columns.pop_back();bad.columns.pop_back();EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns.push_back(bad.columns[0]);EncodeCommunityScore(bad);});
  Reject([]{auto bad=Score();bad.columns[3].integer=1;EncodeCommunityScore(bad);});
  auto body=EncodeCommunityScore(Score());
  Reject([&]{DecodeCommunityColumns(1,body.at("columns"),1024);});
  GuwangeCommunity service("https://example.invalid/api","Player","authored-test");
  Require(Write(service,{Score(13)})==13,"Invalid native score reached network");
  Require(Read(service,Board(0)).error==13,"Invalid native read reached network");
  service.Stop();Require(Read(service,Board(1)).error==995,"Stopped service accepted work");
}
void Probe(const std::string& endpoint) {
  Require(endpoint.starts_with("http://127.0.0.1:"),"Authored probe must use loopback");
  GuwangeCommunity player(endpoint,"GuwangeTransportTest","authored-test"),rival(endpoint,"GuwangeRival","authored-test");
  for(uint32_t mode=0;mode<3;++mode) {
    const auto combined=mode*4+1,character=mode,personal=combined+character+1;
    auto own=Board(combined);own.xuids={kNativeCommunityLocalXuid};
    const auto absent=Read(player,own);
    Require(!absent.error && absent.views[0].total==0 && absent.views[0].rows.size()==1 &&
        absent.views[0].rows[0].rank==0,"Unranked local user row");
    Require(Read(player,Board(combined)).views[0].rows.empty(),"Unranked row became public score");
    const auto best=PersonalBest(combined,personal);
    CheckPersonalBest(Read(player,best),best,0,0);
    Require(!Write(rival,{Score(combined,character,999),Score(personal,character,999)}),"Rival paired score write");
    CheckPersonalBest(Read(player,best),best,0,1);
    const auto public_rows=Read(player,Board(combined));
    Require(public_rows.views[0].rows.size()==1 && public_rows.views[0].rows[0].nickname=="GuwangeRival",
        "Unranked personal row leaked onto public board");
    auto around=best;around.count=10;
    Require(Read(player,around).views[0].rows[0].nickname=="GuwangeRival","Public around-user paging changed");
    Require(!Write(player,{Score(combined,character),Score(personal,character)}),"Native paired score write");
    CheckPersonalBest(Read(player,best),best,12345,2);
    const auto record=Recording(mode,character);const auto bytes=uint32_t(record.size());
    Require(Replay(player,{combined,kNativeCommunityLocalXuid,true,bytes,Recording((mode+1)%3,character)}).error==13,
        "Wrong mode replay reached HTTP");
    for(auto view:{combined,personal}) {
      Require(!Replay(player,{view,kNativeCommunityLocalXuid,true,bytes,record}).error,"Native recording upload");
      Require(!Write(player,{{view,{{0x10008011,1,bytes,0}}}}),"Native size acknowledgment");
      const auto reply=Read(player,Board(view));Require(!reply.error && reply.views.size()==1,"Native board read");
      const auto& row=reply.views[0].rows.at(0);
      Require(row.rating==12345 && row.xuid==kNativeCommunityLocalXuid && Column(row,view==1?4:6)==character &&
          Column(row,view==1?5:4)==0 && Column(row,0xfffa)==bytes,"Native row metadata");
      Require(Replay(player,{view,kNativeCommunityLocalXuid,false,bytes,{}}).record==record,"Exact own recording download");
      Require(Replay(player,{view,kNativeCommunityLocalXuid,false,bytes-1,{}}).error==122,"Native download capacity");
      const auto foreign=Read(rival,Board(view));const auto other=foreign.views[0].rows.at(0).xuid;
      Require(other!=kNativeCommunityLocalXuid && rival.DisplayName(other)=="GuwangeTransportTest","Remote identity");
      Require(Replay(rival,{view,other,false,bytes,{}}).record==record,"Exact remote recording download");
    }
  }
  player.Stop();rival.Stop();
}
}
int main(int argc,char** argv) {
  try {
    Units();
    if(argc==3 && std::string_view(argv[1])=="--probe")Probe(argv[2]);
    else Require(argc==1,"Unknown arguments");
    std::cout<<checks<<" Guwange community checks passed\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
