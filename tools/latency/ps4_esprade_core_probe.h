#pragma once
// Private, identity-pinned observations. Guest instruction bytes are unchanged.
#include "esprade-probe-pins.h"
namespace esprade_core_probe {
using Frame=std::uintptr_t(KT_SYSV*)(void*,void*,void*);
using Apply=std::uintptr_t(KT_SYSV*)(void*,int,const void*,void*);
using Step=std::uintptr_t(KT_SYSV*)(void*,double);
using Extra=int(KT_SYSV*)();
inline Frame original_frame{}; inline Apply original_apply{};
inline Step original_step{}; inline Extra original_extra{};
inline unsigned char* module_image{};
inline thread_local std::uint64_t current_frame{};
template<class T> T Read(const void* p,std::size_t off=0) {
 T v{};if(p)std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(v));return v;
}
inline std::uint64_t Point(std::uint64_t site,std::uint64_t pc,
 std::uint64_t a=0,std::uint64_t b=0,std::uint64_t c=0,std::uint64_t d=0) {
 return evac_latency::Record(evac_latency::Kind::GuestObservation,site,pc,a,b,c,d);
}
inline bool IsEsprade(const Runtime& r) {
 return r.image_name=="esprade_01030.prx" && r.header.source_sha256==kEspradeModuleSha;
}
inline bool CoreObserverEnabled() {
 static const bool enabled=[] {
  char value[2]{};
  return GetEnvironmentVariableA("EVAC_ESPRADE_TEST_NO_CORE",value,sizeof(value))==0;
 }();
 return enabled;
}
std::uintptr_t KT_SYSV FrameProbe(void* context,void* inputs,void* output) {
 const auto begin=Point(100,0xe80,reinterpret_cast<std::uintptr_t>(context),reinterpret_cast<std::uintptr_t>(inputs),reinterpret_cast<std::uintptr_t>(output),reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
 const auto previous=current_frame;current_frame=begin;
 // Explicit private experiment only. This changes the original module's
 // existing look-ahead setting, not guest instructions or the global profile.
 static const int experiment=[] {
  char value[8]{};const auto n=GetEnvironmentVariableA("EVAC_ESPRADE_TEST_AHEAD",value,sizeof(value));
  if(!n)return -1;
  if(n!=1 || value[0]<'0' || value[0]>'2')throw std::runtime_error("Private ESP Ra.De. ahead experiment must be 0..2");
  return int(value[0]-'0');
 }();
 if(experiment>=0) {
  const std::uint32_t count=static_cast<unsigned>(experiment);
  std::memcpy(module_image+0x3403f60,&count,sizeof(count));
 }
 Point(116,0x3403f60,begin,Read<std::uint32_t>(module_image,0x3403f60),Read<std::uint32_t>(module_image,0x3403f64));
 const auto result=original_frame(context,inputs,output);
 Point(101,0xe80,begin,result,Read<std::uintptr_t>(output,24));current_frame=previous;return result;
}
std::uintptr_t KT_SYSV ApplyProbe(void* context,int player,const void* report,void* cache) {
 const auto type=Read<std::uint32_t>(report);const auto bits=type==3?Read<std::uint16_t>(report,8):0;
 const auto begin=Point(102,0x5cc80,current_frame,unsigned(player),(std::uint64_t(type)<<32)|bits,reinterpret_cast<std::uintptr_t>(report));
 if(report)Point(104,0x5cc80,begin,Read<std::uint64_t>(report,8),Read<std::uint64_t>(report,16),Read<std::uint64_t>(report,24));
 const auto result=original_apply(context,player,report,cache);Point(103,0x5cc80,begin,current_frame,unsigned(player),result);return result;
}
int KT_SYSV ExtraProbe() {
 const int result=original_extra();Point(110,0x5ca70,current_frame,unsigned(result));return result;
}
std::uintptr_t KT_SYSV StepProbe(void* context,double duration) {
 std::uint64_t bits{};std::memcpy(&bits,&duration,sizeof(bits));
 const auto begin=Point(112,0x66f60,current_frame,reinterpret_cast<std::uintptr_t>(context),bits);
 Point(114,0x66f60,begin,Read<std::uint64_t>(context,8),Read<std::uint64_t>(context,16),Read<std::uint64_t>(context,24));
 const auto result=original_step(context,duration);
 Point(115,0x66f60,begin,Read<std::uint64_t>(context,8),Read<std::uint64_t>(context,16),Read<std::uint64_t>(context,24));
 Point(113,0x66f60,begin,current_frame,result);return result;
}
inline void Install(Runtime& r) {
 if(!evac_latency::state || !CoreObserverEnabled() || !IsEsprade(r))return;
 auto* slot=reinterpret_cast<std::uintptr_t*>(r.image+0x3400088);
 const auto expected=reinterpret_cast<std::uintptr_t>(r.image+0xe80);
 if(*slot!=expected)throw std::runtime_error("Private ESP Ra.De. callback pin differs");
 module_image=r.image;original_frame=reinterpret_cast<Frame>(expected);*slot=reinterpret_cast<std::uintptr_t>(&FrameProbe);
 Point(99,0x3400088,reinterpret_cast<std::uintptr_t>(r.image),expected,*slot);
}
inline std::uint64_t WrapImport(Runtime& r,const Import& import,std::uint64_t target) {
 if(!evac_latency::state || !CoreObserverEnabled() || !IsEsprade(r) || import.type!=2)return target;
 const auto nid=import.nid.substr(0,import.nid.find('#'));
 unsigned offset{};
 if(nid=="w+dCT5vVoaQ")offset=0x5cc80;
 else if(nid=="DZX8XVO+jNY")offset=0x66f60;
 else if(nid=="kYu2Su2im+I")offset=0x5ca70;
 else return target;
 const auto* provider=ImageAt(target);
 if(!provider || provider->image_name!="emu_integration_01500.prx" || provider->header.source_sha256!=kEspradeIntegrationSha || target!=reinterpret_cast<std::uintptr_t>(provider->image+offset))
  throw std::runtime_error("Private ESP Ra.De. import provider pin differs");
 if(offset==0x5cc80){original_apply=reinterpret_cast<Apply>(target);return reinterpret_cast<std::uintptr_t>(&ApplyProbe);}
 if(offset==0x66f60){original_step=reinterpret_cast<Step>(target);return reinterpret_cast<std::uintptr_t>(&StepProbe);}
 original_extra=reinterpret_cast<Extra>(target);return reinterpret_cast<std::uintptr_t>(&ExtraProbe);
}
}
