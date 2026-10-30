#include "backends/ps4/graphics/presentation_extensions.h"
#include "backends/ps4/graphics/device_requirements.h"
#include "backends/ps4/graphics/snapshot_handoff.h"
#include "backends/ps4/graphics/swapchain_bootstrap.h"
#include "backends/ps4/loader/preparation_policy.h"
#include "backends/ps4/loader/esprade_latency.h"
#include "backends/ps4/content/resource_prepare.h"
#include "backends/ps4/ketsui_version.h"
#include "common/catalogue.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string_view>

namespace {
unsigned checks{};
void Check(bool value, const char* name) {
    ++checks;
    if (!value) throw std::runtime_error(name);
}
template<class F> void Reject(F action, const char* name) {
    bool rejected{};
    try { action(); } catch (const std::runtime_error&) { rejected=true; }
    Check(rejected,name);
}

void Presentation() {
    using namespace cave::ps4::presentation;
    // Enumerate instance capability combinations and device feature/name sets.
    // In particular: both surface names + KHR-only device must retain KHR.
    for (unsigned instance=0;instance<8;++instance) {
        const auto surfaces=SurfaceExtensions([&](std::string_view name) {
            return name==kCapabilities ? bool(instance&1) : name==kSurfaceExt ? bool(instance&2) : bool(instance&4);
        });
        Check(surfaces.ext==bool((instance&1)&&(instance&2)),"EXT surface dependency");
        Check(surfaces.khr==bool((instance&1)&&(instance&4)),"KHR surface dependency");
        for (unsigned device=0;device<8;++device) {
            const auto actual=SwapchainExtension(surfaces,[&](std::string_view name) {
                return name==kSwapchainExt ? bool(device&1) : bool(device&2);
            },bool(device&4));
            const char* expected=nullptr;
            if ((device&4)&&(instance&1)) {
                if ((instance&2)&&(device&1)) expected=kSwapchainExt;
                else if ((instance&4)&&(device&2)) expected=kSwapchainKhr;
            }
            Check((!actual&&!expected)||(actual&&expected&&std::string_view(actual)==expected),"paired presentation extension/feature selection");
        }
    }
}

void SnapshotHandoff() {
    using namespace cave::ps4;
    using S=SnapshotOwnership;
    struct Slot {S state;};
    for(std::uint32_t active_index=0;active_index<3;++active_index)
      for(std::uint32_t ready_index=0;ready_index<3;++ready_index) {
        if(active_index==ready_index)continue;
        std::array<Slot,3> slots{{{S::Free},{S::Free},{S::Free}}};
        slots[active_index].state=S::Presenting;slots[ready_index].state=S::Ready;
        std::optional<std::uint32_t> ready=ready_index;auto active=active_index;
        Check(LatchNewestSnapshot(slots,ready,active),"new ready image is latched after WSI wait");
        Check(active==ready_index && !ready && slots[active_index].state==S::Free && slots[ready_index].state==S::Presenting,
              "late latch preserves one owner and releases only the unused old snapshot");
        Check(!LatchNewestSnapshot(slots,ready,active),"no ready image leaves the active image unchanged");
        slots[active_index].state=S::Writing;ready=active_index;
        Reject([&]{LatchNewestSnapshot(slots,ready,active);},"partial writing image cannot be presented");
        Check(slots[active_index].state==S::Writing && active==ready_index,"invalid handoff cannot change ownership");
      }
    for(bool accepted:{false,true}) {
        unsigned order=0;
        const auto result=PublishThenPace([&]{Check(++order==1,"copy completion and publish precede pacing");},
            [&]{Check(++order==2,"guest pacing still executes exactly once");return accepted;});
        Check(result==accepted && order==2,"guest cancellation is preserved after owned-image publication");
    }
    unsigned paced=0;
    Reject([&]{PublishThenPace([]{throw std::runtime_error("copy failed");},[&]{++paced;return true;});},
           "copy failure propagates");
    Check(!paced,"failed copy must not advance the guest pacing path");
}
void SwapchainBootstrap() {
    struct Info {int presentMode,oldSwapchain,width,images;};
    for(bool prior:{false,true}) for(int mode:{0,1,2}) {
        bool established=prior;
        std::vector<Info> created;std::vector<int> destroyed;
        const auto retire=[&](int* handle){destroyed.push_back(*handle);delete handle;};
        using Owner=std::unique_ptr<int,decltype(retire)>;
        {
            auto chain=cave::ps4::CreateWindowSwapchain(Info{mode,9,1280,3},established,2,
                [&](Info info){created.push_back(info);return Owner(new int(static_cast<int>(created.size())),retire);});
            const bool bootstrap=!prior && mode!=2;
            Check(established && created.size()==(bootstrap?2u:1u),"bootstrap is only needed before the first non-FIFO chain");
            Check(*chain==static_cast<int>(created.size()),"final owner is the requested mode chain");
            Check(created.back().presentMode==mode && created.front().oldSwapchain==9,"requested mode and preceding retirement are preserved");
            for(const auto& info:created) Check(info.width==1280 && info.images==3,"bootstrap preserves format/resource creation parameters");
            if(bootstrap) {
                Check(created.front().presentMode==2 && created.back().oldSwapchain==1,"replacement retires the never-presented bootstrap chain");
                Check(destroyed==std::vector<int>{1},"only the temporary chain is released before return");
            } else Check(destroyed.empty(),"direct creation retains its only live owner");
        }
        Check(destroyed.size()==created.size(),"all created chains have exactly one retirement owner");
    }
    for(unsigned fail_at:{1u,2u}) {
        bool established=false;unsigned called=0;std::vector<int> destroyed;
        const auto retire=[&](int* handle){destroyed.push_back(*handle);delete handle;};
        using Owner=std::unique_ptr<int,decltype(retire)>;
        Reject([&]{
            cave::ps4::CreateWindowSwapchain(Info{0,9,1280,3},established,2,[&](Info) {
                if(++called==fail_at)throw std::runtime_error("creation failed");
                return Owner(new int(static_cast<int>(called)),retire);
            });
        },"bootstrap creation/replacement failures propagate");
        Check(!established,"failed initialization cannot suppress a later bootstrap retry");
        Check(destroyed.size()==fail_at-1,"failure releases the temporary chain exactly once");
    }
}
void DeviceRequirements() {
    using namespace cave::ps4::graphics;
    constexpr std::uint32_t required=(1u<<22)|(3u<<12);
    for (auto version : {0u,(1u<<22),(1u<<22)|(1u<<12),(1u<<22)|(2u<<12)|133u,
                         (1u<<29)|required})
        Reject([&]{RequireVulkanApi(version,required,"AMD Radeon RX 580 2048SP","2.0.137");},
               "old or incompatible device API is rejected without an assertion");
    for (auto version : {required,required|1u,(1u<<22)|(4u<<12),2u<<22}) {
        RequireVulkanApi(version,required,"supported GPU","test driver");
        Check(true,"supported Vulkan API remains accepted");
    }
    Check(ApiVersionText((1u<<22)|(2u<<12)|133u)=="1.2.133","reported device API formatting");
    try {
        RequireVulkanApi((1u<<22)|(2u<<12)|133u,required,"AMD Radeon RX 580 2048SP","2.0.137");
        Check(false,"old RX 580 driver was accepted");
    } catch (const std::runtime_error& error) {
        const std::string_view message=error.what();
        for (auto part : {"Vulkan 1.3.0", "RX 580 2048SP", "2.0.137", "1.2.133", "Update the graphics driver"})
            Check(message.find(part)!=std::string_view::npos,"old driver error is actionable and preserves GPU identity");
    }
}

void Policies() {
    using namespace cave::ps4;
    // Exercise the actual generated catalogue, not only hand-written policy inputs.
    for (const auto* id : {"ketsui", "daioujou", "esprade", "feveron"}) {
        const auto& game = cave::FindGame(id);
        Check(!game.variants.empty(), "PS4 catalogue supplies preparation identity");
        for (const auto& variant : game.variants) {
            const auto policy = PlanPs4Data(id, variant.version, variant.layout);
            Check(!policy.programs.empty(), "every shipped PS4 variant has a preparation policy");
            for (const auto& program : policy.programs)
                Check(std::any_of(variant.files.begin(), variant.files.end(), [&](const auto& file) {
                    return file.path == program && file.program;
                }), "policy module exists in the selected manifest");
        }
    }
    const auto base=PlanPs4Data("daioujou","01.00","base");
    Check(base.programs==std::vector<std::string>{"eboot.bin","emu_integration_01700.prx","daioujou_01000.prx"},"DOJ base must not load update modules");
    Check(!base.merged_app&&base.read_fixes&&!base.community_offline&&base.replay_peer&&base.local_replay,"DOJ base community recipe");
    std::vector<std::wstring> arguments{L"--community-provider",L"configured"};
    AppendVersionArguments(arguments,base.merged_app,base.community_offline);
    Check(arguments==std::vector<std::wstring>{L"--community-provider",L"configured"},"DOJ base launcher does not force offline");
    for (bool configured:{false,true}) {
        Check(CommunityEnabledForData(configured,base.community_offline)==configured,"DOJ base honors community preference");
        Check(CommunityEnabledForData(configured,false)==configured,"updated data honors online setting");
    }
    for (const auto* game:{"daioujou","esprade"}) {
        const auto updated=PlanPs4Data(game,"01.04","merged-patch");
        Check(updated.programs.size()==6&&updated.merged_app&&updated.read_fixes&&!updated.community_offline&&updated.local_replay,"01.04 preparation unchanged");
        Check(updated.replay_peer==(std::string_view(game)=="daioujou"),"replay peer fix is title scoped");
        arguments.clear();AppendVersionArguments(arguments,updated.merged_app,updated.community_offline);
        Check(arguments==std::vector<std::wstring>{L"--merged-app",L"on"},"01.04 never forced offline");
    }
    for (bool updated:{false,true}) {
        const auto ketsui=PlanPs4Data("ketsui",updated?"01.02":"01.00",updated?"merged-patch":"base");
        Check(ketsui.programs.size()==3&&ketsui.read_fixes&&!ketsui.community_offline&&ketsui.merged_app==updated,"Ketsui base/update recipe");
    }
    Reject([]{PlanPs4Data("daioujou","01.04","base");},"invalid update/base combination");
    Reject([]{PlanPs4Data("daioujou","01.00","merged-patch");},"mixed DOJ data is not base");
    Reject([]{PlanPs4Data("daioujou","01.01","base");},"unknown DOJ version");
    Reject([]{PlanPs4Data("esprade","01.00","base");},"no accidental ESP base support");
    const auto feveron=PlanPs4Data("feveron","01.00","base");
    Check(feveron.programs==std::vector<std::string>{"eboot.bin","emu_integration_01200.prx","dfeveron_01000.prx","dfeveron_01010.prx"},
        "Feveron base includes both shipped core revisions");
    Check(!feveron.merged_app&&feveron.read_fixes&&!feveron.replay_peer&&feveron.local_replay,
        "Feveron prepares only its own local replay viewer adaptation");
    Reject([]{PlanPs4Data("feveron","01.01","base");},"Feveron master version is not application version");
    Reject([]{PlanPs4Data("feveron","01.00","merged-patch");},"unsupported Feveron merged version rejected");
    const auto garegga=PlanPs4Data("garegga","01.00","base");
    Check(garegga.programs==std::vector<std::string>{"eboot.bin","emu_integration.prx","emu_integration_01100.prx",
        "garegga_01000.prx","garegga_01010.prx","garegga_01020.prx","garegga_01030.prx","garegga_01040.prx"},
        "Garegga includes both integration modules and all five native core revisions");
    Check(garegga.local_replay&&!garegga.replay_peer,"Garegga local viewer keeps its own identity flow");
    Reject([]{PlanPs4Data("garegga","01.01","base");},"Garegga master version is not application version");
    Reject([]{PlanPs4Data("garegga","01.00","merged-patch");},"unsupported Garegga merged data rejected");
    Reject([]{PlanPs4Data("other","01.00","base");},"unknown title");
    // The wrapper changes identity, not the 01.00 native hook/replay layout.
    const auto* elf=FindKetsuiVersion("22927d470a261d52c31510b8e5f173f4f6256307828e302f6eab19f4357c0e8f");
    const auto* self=FindKetsuiVersion("9113b52b4b61c71cf557a74f9d9e3dc29af91f572c245cdec5ba09c7e1b0a8ed");
    Check(elf&&self&&elf->version==self->version&&elf->archive_seed_load==self->archive_seed_load&&
        elf->factory==self->factory&&elf->factory_slot==self->factory_slot&&
        elf->job_targets==self->job_targets&&elf->JobSlots()==self->JobSlots()&&
        elf->backend_slots==self->backend_slots&&elf->backend_targets==self->backend_targets&&
        elf->psb_root==self->psb_root&&elf->psb_has==self->psb_has&&elf->psb_item==self->psb_item&&
        elf->psb_integer==self->psb_integer&&elf->copy_profile==self->copy_profile&&
        elf->module_loaded==self->module_loaded&&elf->context==self->context&&
        elf->service_label==self->service_label&&elf->replay_data_version==self->replay_data_version,
        "SELF wrapper selects base 01.00 offsets/replay");
    Check(!FindKetsuiVersion("unknown"),"unknown Ketsui image rejected");
}

using Bytes=cave::ps4::ResourceBytes;
void Put(Bytes& bytes,std::size_t offset,std::uint64_t value,unsigned width) {
    for (unsigned i=0;i<width;++i) bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));
}
Bytes Elf() {
    Bytes data(512);
    const std::array<std::uint8_t,7> magic{0x7f,'E','L','F',2,1,1};
    std::copy(magic.begin(),magic.end(),data.begin());
    Put(data,32,64,8);Put(data,54,56,2);Put(data,56,1,2);
    Put(data,64,1,4);Put(data,72,256,8);Put(data,80,0,8);Put(data,96,256,8);
    data[288]=0x48;data[289]=0xb8;data[298]=0x48;data[299]=0xb9;
    for (unsigned i=0;i<8;++i) data[300+i]=static_cast<std::uint8_t>(i+1);
    for (unsigned i=0;i<5;++i) data[293+i]=static_cast<std::uint8_t>(i+9);
    return data;
}
Bytes Self(const Bytes& elf) {
    Bytes data(512);
    const std::array<std::uint8_t,10> magic{0x4f,0x15,0x3d,0x1d,0,1,1,0x12,1,1};
    std::copy(magic.begin(),magic.end(),data.begin());
    Put(data,12,256,2);Put(data,16,512,8);Put(data,24,1,2);
    Put(data,32,0x800,8);Put(data,40,256,8);Put(data,48,256,8);Put(data,56,256,8);
    std::copy_n(elf.begin(),120,data.begin()+64);
    std::copy(elf.begin()+256,elf.end(),data.begin()+256);
    return data;
}
void Seeds() {
    using cave::ps4::ReadArchiveSeed;
    const Bytes expected{1,2,3,4,5,6,7,8,9,10,11,12,13};
    const auto elf=Elf(),self=Self(elf);
    Check(ReadArchiveSeed(elf,32)==expected,"ELF archive seed");
    Check(ReadArchiveSeed(self,32)==expected,"SELF archive seed equals ELF");
    Check(elf==Elf()&&self==Self(elf),"archive inputs remain unchanged");
    auto bad=self;bad[32]|=2;Reject([&]{ReadArchiveSeed(bad,32);},"encrypted SELF rejected");
    bad=self;bad.resize(300);Reject([&]{ReadArchiveSeed(bad,32);},"truncated SELF rejected");
    bad=elf;bad[288]=0;Reject([&]{ReadArchiveSeed(bad,32);},"changed seed instruction rejected");
    Reject([&]{ReadArchiveSeed(elf,250);},"out of segment seed rejected");
    auto relative=elf;
    relative[288]=0x48;relative[289]=0x8d;relative[290]=0x35;Put(relative,291,64-39,4);
    relative[295]=0xba;Put(relative,296,13,4);
    std::copy(expected.begin(),expected.end(),relative.begin()+320);
    Check(ReadArchiveSeed(relative,32,true)==expected,"DOJ relative ELF seed unchanged");
}
}
void EspradeLatency() {
    using namespace cave::ps4;
    constexpr std::array<std::string_view,4> identities{
        "46fb512a30b680784a0f792556a86ced58305fcff4475610c10dd892bc72c10e",
        "9c8d0a7f77e983b1379d2f469c2e510e8345e3f1668b642a2600b3255da13396",
        "0896b435c78ae9438c95d88927961f90b241c16a845dce9d376ccb40da063a18",
        "96d673c91afe2ecc03c45ae316cb10573f1007627fa9c279af4d912585e64953"};
    const auto hash=[](std::string_view text) {
        std::array<std::uint8_t,32> result{};
        const auto hex=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for (std::size_t i=0;i<result.size();++i) result[i]=static_cast<std::uint8_t>((hex(text[i*2])<<4)|hex(text[i*2+1]));
        return result;
    };
    const std::vector<std::uint8_t> original{0x83,0xf8,0x01,0xb8,0x02,0,0,0,0x83,0xd8,0,0xc3,0x31,0xc0,0xc3};
    for (auto identity:identities) {
        auto code=original;
        Check(ApplyEspradeLatency(code,0x10ef,hash(identity)),"supported ESP revision adapted");
        Check(std::equal(code.begin(),code.begin()+8,original.begin()) && code[8]==0x90 && code[9]==0x90 && code[10]==0x90 &&
              std::equal(code.begin()+11,code.end(),original.begin()+11),"only base-machine subtraction changes; disabled branch preserved");
    }
    auto untouched=original;
    Check(!ApplyEspradeLatency(untouched,0x10ef,{} )&&untouched==original,"other original images remain unchanged");
    auto bad=original;bad[3]^=1;const auto before=bad;
    Reject([&]{ApplyEspradeLatency(bad,0x10ef,hash(identities[0]));},"different selector rejected");
    Check(bad==before,"rejected selector not partly patched");
    Reject([&]{ApplyEspradeLatency(std::span(untouched).first(10),0x10ef,hash(identities[0]));},"truncated selector rejected");
    Check(!ApplyEspradeLatency(untouched,0x2000,hash(identities[0]))&&untouched==original,"unrelated segment not written");
}

int main() {
    try {
        Presentation();SnapshotHandoff();SwapchainBootstrap();DeviceRequirements();Policies();Seeds();EspradeLatency();
        std::printf("%u PS4 compatibility checks passed (no ROM or GPU required)\n",checks);return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"PS4 compatibility check %u failed: %s\n",checks,error.what());return 1;
    }
}
