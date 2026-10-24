#include "community/replay_format.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <string_view>

namespace guwange {
namespace {
void Check(bool ok) { if(!ok)throw std::runtime_error("Invalid Guwange replay"); }
uint32_t Be32(std::span<const uint8_t> data,size_t at) {
  return uint32_t(data[at])<<24|uint32_t(data[at+1])<<16|
      uint32_t(data[at+2])<<8|data[at+3];
}
uint64_t Be64(std::span<const uint8_t> data,size_t at) {
  return uint64_t(Be32(data,at))<<32|Be32(data,at+4);
}
}
ReplayInfo InspectReplay(std::span<const uint8_t> data) {
  // Base 0.0.0.4: header builder/finalizer 821934B8/821936C8,
  // signed-byte checksum 821935A0, serialization/player selection 821BFD10.
  // Native local Blue and Arrange recordings independently confirm this layout.
  Check(data.size()>=kReplayHeaderBytes+kReplayFixedPayloadBytes && data.size()<=kReplayMaxBytes);
  const auto payload=Be32(data,36),mode=Be32(data,52),version=Be32(data,56);
  Check(payload==data.size()-kReplayHeaderBytes && Be32(data,48)==2 && mode<3 && version==0);
  constexpr std::array<std::string_view,3> magic{
      "GWN_for_XB360_AC20100820","GWN_for_XB360_BL20100820","GWN_for_XB360_XB20100820"};
  const auto expected=magic[mode];
  Check(std::equal(expected.begin(),expected.end(),data.begin()) && data[expected.size()]==0);
  // The original strcmp ignores bytes after NUL; real recordings have nonzero padding.
  int32_t sum=0;
  for(auto value:data.subspan(kReplayHeaderBytes))sum+=value<128?int(value):int(value)-256;
  Check(uint16_t(sum)==(uint16_t(data[32])<<8|data[33]));
  const auto filetime=Be64(data,40);
  Check(filetime>0 && filetime<=INT64_MAX);
  // Native writer uses lwz/extsh, then removes the extra stage slot at >=5.
  int32_t stage=std::bit_cast<int16_t>(uint16_t(Be32(data,60)));
  if(stage>=5)--stage;
  ReplayInfo info{filetime,mode,version,payload,stage,
      {{{Be32(data,76),Be32(data,80)},{Be32(data,104),Be32(data,108)}}}};
  bool active=false;
  for(const auto& player:info.players) {
    Check(player.character<=2 || player.character==UINT32_MAX);
    active|=player.character!=UINT32_MAX;
  }
  Check(active);
  return info;
}
}
