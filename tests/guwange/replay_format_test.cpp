#include "community/replay_format.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
int checks=0;
void Require(bool value,const char* message) {
  ++checks;if(!value)throw std::runtime_error(message);
}
template<class F> void Reject(F action) {
  ++checks;
  try{action();}catch(const std::exception&){return;}
  throw std::runtime_error("Malformed Guwange recording accepted");
}
void Put32(std::vector<uint8_t>& data,size_t at,uint32_t value) {
  for(int i=3;i>=0;--i){data[at+i]=uint8_t(value);value>>=8;}
}
void Checksum(std::vector<uint8_t>& data) {
  int64_t sum=0;
  for(size_t i=256;i<data.size();++i)sum+=data[i]>=0x80?int(data[i])-256:data[i];
  const auto value=uint16_t(sum);data[32]=uint8_t(value>>8);data[33]=uint8_t(value);
}
std::vector<uint8_t> Recording(uint32_t mode) {
  // Authored data only. No original program, state snapshot or input stream.
  std::vector<uint8_t> data(256+0x2cab8+67,0xa5);
  constexpr std::string_view names[]{"GWN_for_XB360_AC20100820",
      "GWN_for_XB360_BL20100820","GWN_for_XB360_XB20100820"};
  std::copy(names[mode].begin(),names[mode].end(),data.begin());data[names[mode].size()]=0;
  Put32(data,36,uint32_t(data.size()-256));Put32(data,40,0x01dd63a6);Put32(data,44,0x1a501600);
  Put32(data,48,2);Put32(data,52,mode);Put32(data,56,0);Put32(data,60,5);
  Put32(data,76,2);Put32(data,80,12345);Put32(data,104,UINT32_MAX);Put32(data,108,0);
  Checksum(data);return data;
}
void Units() {
  for(uint32_t mode=0;mode<3;++mode) {
    const auto bytes=Recording(mode);const auto info=guwange::InspectReplay(bytes);
    Require(info.mode==mode && info.play_kind==0 && info.stage==4,"Mode/play kind/native stage mapping");
    auto score_attack=bytes;Put32(score_attack,56,1);
    Require(guwange::InspectReplay(score_attack).play_kind==1,"Native Score Attack recording rejected");
    for(uint32_t kind:{2u,3u,4u,UINT32_MAX})
      Reject([&]{auto bad=bytes;Put32(bad,56,kind);guwange::InspectReplay(bad);});
    Require(info.filetime==0x01dd63a61a501600ULL && info.payload_bytes==bytes.size()-256,"Native time/payload");
    Require(info.players[0].character==2 && info.players[0].score==12345 &&
        info.players[1].character==UINT32_MAX,"Player snapshot");
    for(auto at:{0u,22u,32u,36u,48u,52u,56u,76u,104u,256u})
      Reject([&]{auto bad=bytes;bad[at]^=0x10;guwange::InspectReplay(bad);});
    Reject([&]{auto bad=bytes;bad.pop_back();guwange::InspectReplay(bad);});
    auto padding=bytes;padding[25]=0x7f;padding[34]=0x93;padding[180]=0x34;
    Require(guwange::InspectReplay(padding).mode==mode,"Unused header padding rejected");
  }
  auto bytes=Recording(0);
  for(uint32_t raw:{0u,4u,5u,6u,0xffffu,0x12340005u}) {
    Put32(bytes,60,raw);const int32_t native=int16_t(raw&0xffff);
    Require(guwange::InspectReplay(bytes).stage==(native>=5?native-1:native),"Signed low-half stage");
  }
  Put32(bytes,104,1);Put32(bytes,108,0xf1234567);
  Require(guwange::InspectReplay(bytes).players[1].score==0xf1234567,"Second player raw score changed");
  Put32(bytes,76,UINT32_MAX);
  Require(guwange::InspectReplay(bytes).players[1].character==1,"Second-player-only recording");
  Reject([&]{auto bad=bytes;Put32(bad,104,UINT32_MAX);guwange::InspectReplay(bad);});
  for(uint32_t value:{0u,1u,UINT32_MAX})
    Reject([&]{auto bad=bytes;Put32(bad,48,value);guwange::InspectReplay(bad);});
  Reject([&]{auto bad=bytes;Put32(bad,36,UINT32_MAX);guwange::InspectReplay(bad);});
  Reject([&]{auto bad=bytes;Put32(bad,40,0);Put32(bad,44,0);guwange::InspectReplay(bad);});
  Reject([&]{auto bad=bytes;Put32(bad,40,0x80000000);guwange::InspectReplay(bad);});
  for(size_t size:{size_t(0),size_t(255),size_t(256),size_t(256+0x2cab8-1)})
    Reject([&]{guwange::InspectReplay(std::span(bytes).first(size));});
  Reject([&]{auto bad=bytes;bad.resize(guwange::kReplayMaxBytes+1);Put32(bad,36,uint32_t(bad.size()-256));Checksum(bad);guwange::InspectReplay(bad);});
  auto minimum=bytes;minimum.resize(256+0x2cab8);Put32(minimum,36,0x2cab8);Checksum(minimum);
  Require(guwange::InspectReplay(minimum).payload_bytes==0x2cab8,"Fixed-only payload rejected");
  auto unsigned_sum=bytes;uint64_t sum=0;
  for(size_t i=256;i<bytes.size();++i)sum+=bytes[i];
  unsigned_sum[32]=uint8_t(sum>>8);unsigned_sum[33]=uint8_t(sum);
  Reject([&]{guwange::InspectReplay(unsigned_sum);});
}
}
int main(int argc,char** argv) {
  try {
    Units();std::cout<<checks<<" authored Guwange replay checks passed\n";
    for(int i=1;i<argc;++i) {
      std::ifstream input(argv[i],std::ios::binary);Require(bool(input),"Cannot open local recording");
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
      const auto info=guwange::InspectReplay(bytes);
      std::cout<<"record mode="<<info.mode<<" play_kind="<<info.play_kind<<" stage="<<info.stage<<" bytes="<<bytes.size()
          <<" p1_character="<<info.players[0].character<<" p1_score="<<info.players[0].score
          <<" p2_character="<<info.players[1].character<<" p2_score="<<info.players[1].score<<'\n';
    }
    return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
