#include "community_client.h"
#include "community_runtime.h"
#include "replay_format.h"
#include <rex/cvar.h>
#include <rex/input/gamepad_mapping.h>
#include <rex/runtime.h>
#include <rex/ppc.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace std::chrono_literals;
extern "C" void __imp__XamShowGamerCardUIForXUID(PPCContext&,uint8_t*);
namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), {});
}
void ConfigTest(const std::filesystem::path& directory) {
  const auto path = directory / "settings-test.toml";
  rex::cvar::FlagRegistrar test({.name="community_test_string", .type=rex::cvar::FlagType::String,
      .category="Test", .description="test", .setter=[](auto){return true;}, .getter=[](){return "quote\"slash\\\n水母";}, .default_value=""});
  { std::ofstream file(path); file << "unknown = 'retain this'\n[future]\nvalue = 7\n"; }
  Check(rex::cvar::SaveConfigChecked(path), "config save failed");
  auto value = Read(path);
  Check(value.find("retain this") != std::string::npos, "unknown key lost");
  Check(value.find("value = 7") != std::string::npos, "unknown table lost");
  Check(std::filesystem::exists(path.wstring() + L".bak"), "backup missing");
  { std::ofstream file(path); file << "broken = ["; }
  Check(!rex::cvar::SaveConfigChecked(path) && Read(path) == "broken = [", "broken config replaced");
  rex::cvar::UnregisterFlag("community_test_string");
}
void PadTest() {
  rex::input::X_INPUT_GAMEPAD input{};
  input.buttons = 0x1000;
  input.left_trigger = 128;
  input.thumb_lx = 2345;
  auto identity = input;
  rex::input::RemapPhysicalGamepad(identity);
  Check(uint16_t(identity.buttons) == 0x1000 && identity.left_trigger == 128 && int16_t(identity.thumb_lx) == 2345, "identity mapping changed pad");
  Check(rex::cvar::SetFlagByName("pad_a", "B"), "pad_a missing");
  Check(rex::cvar::SetFlagByName("pad_b", "A"), "pad_b missing");
  auto mapped = input;
  rex::input::RemapPhysicalGamepad(mapped);
  Check(uint16_t(mapped.buttons) == 0x2000, "physical button swap wrong");
  auto extra = input;
  extra.buttons = 0x83c0;  // Y, LB/RB, LS/RS: not hidden or removed by common bindings.
  rex::input::RemapPhysicalGamepad(extra);
  Check(uint16_t(extra.buttons) == 0x83c0, "common button swap changed advanced pad buttons");
  rex::cvar::ResetToDefault("pad_a"); rex::cvar::ResetToDefault("pad_b");
  mapped = input;
  rex::input::RemapPhysicalGamepad(mapped);
  Check(uint16_t(mapped.buttons) == 0x1000, "restore defaults not propagated");
  rex::cvar::SetFlagByName("pad_rt", "A");
  mapped = input;
  rex::input::RemapPhysicalGamepad(mapped);
  Check(mapped.right_trigger == 255 && uint16_t(mapped.buttons) == 0x1000, "shared binding incorrectly unbound");
  rex::cvar::ResetToDefault("pad_rt");
}
void ReplayTest() {
  using rex::system::xam::CommunityPageAccessAllowed;
  Check(!CommunityPageAccessAllowed(0,false) && CommunityPageAccessAllowed(1,false)
      && CommunityPageAccessAllowed(3,true) && CommunityPageAccessAllowed(5,false)
      && !CommunityPageAccessAllowed(5,true) && CommunityPageAccessAllowed(7,true),
      "guest span permissions rejected executable-readable pages or allowed readonly writes");
  std::vector<uint8_t> record(160);
  auto write = [&](size_t offset, uint32_t value) {
    for (size_t i=0;i<4;++i) record[offset+i] = uint8_t(value >> ((3-i)*8));
  };
  write(0,kFutariReplayVersion); write(8,156); write(12,kFutariReplayVersion);
  auto container = FutariWrapReplay(record);
  auto slots = FutariReadReplaySlots(container);
  Check(slots.size()==1 && slots[0].index==0 && slots[0].size==160, "replay slot parsing wrong");
  Check(FutariExtractReplay(container,0)==container, "replay extraction changed bytes");
  auto rejects = [](const std::vector<uint8_t>& bad) {
    try { FutariReadReplaySlots(bad); return false; } catch (const std::exception&) {return true;}
  };
  auto broken=container; broken[0]=0xff; Check(rejects(broken), "bad replay offset accepted");
  broken=container; broken.pop_back(); Check(rejects(broken), "truncated replay accepted");
  broken=container; broken[792]=0xff; Check(rejects(broken), "incompatible replay version accepted");
  auto storage=std::vector<uint8_t>(160);
  rex::memory::store_and_swap<uint32_t>(storage.data(),uint32_t(storage.size()));
  rex::memory::store_and_swap<uint32_t>(storage.data()+4,kFutariReplayVersion);
  FutariValidateStorageReplay(storage);
  for(size_t offset:{size_t(0),size_t(4)}) {
    auto invalid=storage;invalid[offset]^=0xff;
    bool rejected=false;try{FutariValidateStorageReplay(invalid);}catch(const std::exception&){rejected=true;}
    Check(rejected,"invalid storage replay length/version accepted");
  }
  bool rejected_local=false;
  try{FutariValidateStorageReplay(record);}catch(const std::exception&){rejected_local=true;}
  Check(rejected_local,"local save slot accepted as a native network replay");
}
void ReplayPathAbiTest(const std::string& endpoint,const std::filesystem::path& directory) {
  using namespace rex::system::xam;
  const auto profile=directory/"path-abi";
  std::filesystem::create_directories(profile);
  rex::Runtime runtime(profile,profile,{},profile/"cache",{});
  rex::RuntimeConfig config;config.tool_mode=true;
  Check(runtime.Setup(std::move(config))==0,"native path test runtime setup failed");
  auto provider=std::make_shared<FutariCommunity>(endpoint,"PathAbiTest","test-build",profile);
  SetCommunityService(provider);
  struct ResetService { ~ResetService(){SetCommunityService({});} } reset;
  auto* memory=runtime.memory();auto* kernel=runtime.kernel_state();
  const auto address=memory->SystemHeapAlloc(256);
  Check(address!=0,"native path test allocation failed");
  auto* bytes=memory->TranslateVirtual(address);
  auto put=[&](uint32_t offset,uint32_t value){rex::memory::store_and_swap<uint32_t>(bytes+offset,value);};
  auto get=[&](uint32_t offset){return rex::memory::load_and_swap<uint32_t>(bytes+offset);};
  put(0,7001);put(4,200);
  std::memset(bytes+8,0xcc,248);
  // The filename is unused by the community namespace. It must neither be
  // dereferenced nor become part of a host path (including for a title constant
  // whose page is not represented by the guest heap's access metadata).
  Check(BuildCommunityReplayPath(kernel,0,0,1,address,4,1,address+8,address+4)==0,
      "unused native filename rejected a valid replay path");
  const auto expected=CommunityReplayPath(7001,0xB13EBABEBABEBABEull);
  const auto required=uint32_t((expected.size()+1)*2);
  Check(get(4)==required,"native path length is not in bytes");
  for(size_t i=0;i<expected.size();++i)
    Check(rex::memory::load_and_swap<uint16_t>(bytes+8+i*2)==expected[i],"native path UTF16 bytes wrong");
  Check(!rex::memory::load_and_swap<uint16_t>(bytes+8+expected.size()*2)
      && bytes[8+required]==0xcc,"native path terminator or output canary wrong");
  put(4,2);std::memset(bytes+8,0xcc,248);
  Check(BuildCommunityReplayPath(kernel,0,0,1,address,4,0,address+8,address+4)==122
      && get(4)==required && bytes[8]==0xcc,"short native path buffer was written");
  put(4,200);
  Check(BuildCommunityReplayPath(kernel,0,0,2,address,4,0,address+8,address+4)==87,
      "invalid storage facility accepted");
  Check(BuildCommunityReplayPath(kernel,0,0,1,1,4,0,address+8,address+4)==87,
      "unreadable replay info accepted");
  Check(BuildCommunityReplayPath(kernel,0,0,1,address,4,0,1,address+4)==87,
      "unwritable replay path destination accepted");
  put(4,200);
  Check(BuildCommunityReplayPath(kernel,254,0xB000000100000002ull,1,address,4,0,address+8,address+4)==0,
      "remote replay owner was truncated or rejected");
  std::u16string actual;
  for(size_t i=0;i<expected.size();++i)actual.push_back(char16_t(rex::memory::load_and_swap<uint16_t>(bytes+8+i*2)));
  uint32_t view=0;uint64_t owner=0;
  Check(ParseCommunityReplayPath(actual,view,owner) && view==7001 && owner==0xB000000100000002ull,
      "native remote replay path lost owner high bits");
  memory->SystemHeapFree(address);
}
void ProviderTest(const std::string& endpoint,const std::filesystem::path& directory,const std::vector<uint8_t>& record) {
  using namespace rex::system::xam;
  const auto profile=directory/"provider";
  FutariCommunity provider(endpoint,"NativeTest","test-build",profile);
  {
    std::promise<CommunityRankResult> ranks;auto future=ranks.get_future();
    provider.Estimate({{7001,2500}},[&](auto result){ranks.set_value(std::move(result));});
    Check(future.wait_for(5s)==std::future_status::ready,"native rank estimate timed out");
    auto result=future.get();Check(!result.error && result.ranks==std::vector<uint32_t>{1},"empty board estimated rank was not first");
  }
  {
    std::promise<CommunityResult> empty;auto future=empty.get_future();
    provider.Read({1,1,1,{0xB13EBABEBABEBABEull},{{7001,{1,2,9,0x8011}}}},
        [&](auto result){empty.set_value(std::move(result));});
    Check(future.wait_for(5s)==std::future_status::ready,"unranked user stats timed out");
    auto result=future.get();
    Check(!result.error && result.views.size()==1 && !result.views[0].total && result.views[0].rows.size()==1,
        "explicit user stats omitted unranked player");
    const auto& row=result.views[0].rows[0];
    Check(!row.rank && !row.rating && row.columns[0].type==2 && !row.columns[0].integer,
        "unranked player did not have zero/default native stats");
    std::promise<CommunityResult> board;auto board_future=board.get_future();
    provider.Read({1,1,1,{},{{7001,{1,9}}}},[&](auto result){board.set_value(std::move(result));});
    Check(board_future.wait_for(5s)==std::future_status::ready && board_future.get().views[0].rows.empty(),
        "unranked default leaked into the server leaderboard");
  }
  {
    std::promise<uint32_t> rejected;auto result=rejected.get_future();
    provider.Write({{7002,{{0x20008000,2,2500,0}}}},[&](auto error){rejected.set_value(error);});
    Check(result.wait_for(5s)==std::future_status::ready && result.get()!=0,
        "unknown title score schema was submitted");
    Check(!std::filesystem::exists(profile/"community/outbox"),"invalid score created a journal");
  }
  // All 96 native view keys from the title's leaderboard constructor. Keep
  // revisions, difficulties and character filters distinct, including the
  // Arrange family with only one character-independent board per difficulty.
  std::vector<uint32_t> native_views;
  for(uint32_t family=1;family<=8;++family)for(uint32_t difficulty=0;difficulty<3;++difficulty) {
    const uint32_t characters=family==3?1:(family==5 || family==6?3:5);
    for(uint32_t character=0;character<characters;++character)
      native_views.push_back(family*100+difficulty*10+character);
  }
  Check(native_views.size()==96,"native view coverage changed");
  for(size_t first=0;first<native_views.size();first+=16) {
    std::vector<CommunityWrite> writes;
    CommunityRead read{1,1,1,{},{}};
    for(size_t i=first;i<std::min(first+16,native_views.size());++i) {
      const auto view=native_views[i];
      writes.push_back({view,{{0x20000001,2,int64_t(view)*100,0}}});
      read.specs.push_back({view,{1,8}});
    }
    std::promise<uint32_t> submitted;auto write_future=submitted.get_future();
    provider.Write(std::move(writes),[&](auto error){submitted.set_value(error);});
    Check(write_future.wait_for(5s)==std::future_status::ready && !write_future.get(),"native view batch submission failed");
    std::promise<CommunityResult> received;auto read_future=received.get_future();
    provider.Read(read,[&](auto result){received.set_value(std::move(result));});
    Check(read_future.wait_for(5s)==std::future_status::ready,"native view batch lookup timed out");
    const auto result=read_future.get();
    Check(!result.error && result.views.size()==read.specs.size(),"native view batch results missing");
    for(size_t i=0;i<result.views.size();++i) {
      const auto& view=result.views[i];
      Check(view.view==read.specs[i].view && view.total==1 && view.rows.size()==1
          && view.rows[0].rating==int64_t(view.view)*100,"native mode/difficulty/character boards were mixed");
    }
  }
  CommunityWrite write{7001,{{0x20000001,2,2500,0},{0x10000009,1,7,0},
      {0x20000002,2,9007199254740993ll,0},{0x10008011,1,1634,0}}};
  std::promise<uint32_t> submitted;auto submitted_result=submitted.get_future();
  provider.Write({write},[&](auto error){submitted.set_value(error);});
  Check(submitted_result.wait_for(5s)==std::future_status::ready && submitted_result.get()==0,"native provider score submission failed");
  CommunityRead request{0,0xB13EBABEBABEBABEull,10,{},{{7001,{9,2}}}};
  std::promise<CommunityResult> read;auto read_result=read.get_future();
  provider.Read(request,[&](auto result){read.set_value(std::move(result));});
  Check(read_result.wait_for(5s)==std::future_status::ready,"native provider read timed out");
  auto result=read_result.get();
  Check(result.error==0 && result.views.size()==1 && result.views[0].rows.size()==1,"native provider results wrong");
  Check(result.views[0].rows[0].rating==2500 && result.views[0].rows[0].nickname=="NativeTest","native provider lost score or nickname");
  auto attachment=std::find_if(result.views[0].rows[0].columns.begin(),result.views[0].rows[0].columns.end(),
      [](const auto& col){return col.id==0x8011;});
  Check(attachment!=result.views[0].rows[0].columns.end() && attachment->integer==0,
      "unshared local replay advertised as downloadable");
  auto absent=std::find_if(result.views[0].rows[0].columns.begin(),result.views[0].rows[0].columns.end(),
      [](const auto& col){return col.id==1;});
  Check(absent!=result.views[0].rows[0].columns.end() && absent->type==1 && !absent->integer,
      "nonzero score advertised an unshared replay in the Info menu");
  auto capacity=CommunityStatsBufferSize(request.specs,10);
  std::vector<uint8_t> bytes(capacity+16,0xa5);
  Check(SerializeCommunityStats(result,request.specs,0x40001000,{bytes.data(),capacity}),"native stats serialization failed");
  auto u32=[&](size_t offset){return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|(uint32_t(bytes[offset+2])<<8)|bytes[offset+3];};
  Check(u32(0)==1 && u32(4)==0x40001008 && u32(8)==7001,"native view header/pointer wrong");
  Check(u32(16)==1 && u32(20)==0x40001018 && u32(32)==1,"native row pointer/rank wrong");
  Check(u32(64)==2 && u32(68)==0x40001048 && bytes[80]==1 && u32(88)==7,"native columns wrong");
  Check(bytes[104]==2 && u32(112)==0x00200000 && u32(116)==1,"native int64 column precision lost");
  Check(std::all_of(bytes.begin()+capacity,bytes.end(),[](auto b){return b==0xa5;}),"native serializer overran destination");
  Check(!SerializeCommunityStats(result,request.specs,0x40001000,{bytes.data(),10}),"native serializer accepted undersized buffer");
  auto localized=result;
  auto& localized_row=localized.views[0].rows[0];
  localized_row.nickname="琥珀水母-ABCD";
  localized_row.native_tag=FutariNativeNickname(localized_row.nickname);
  Check(SerializeCommunityStats(localized,request.specs,0x40001000,{bytes.data(),capacity})
      && !std::memcmp(bytes.data()+48,localized_row.native_tag.data(),localized_row.native_tag.size())
      && bytes[48+localized_row.native_tag.size()]==0,"native serializer damaged short display tag or terminator");
  localized_row.native_tag=std::string(16,'x');
  Check(!SerializeCommunityStats(localized,request.specs,0x40001000,{bytes.data(),capacity}),
      "native serializer accepted an overflowing legacy tag");
  auto transfer=[&](FutariCommunity& service,CommunityReplay request) {
    std::promise<CommunityReplayResult> done;auto future=done.get_future();
    service.Replay(std::move(request),[&](auto reply){done.set_value(std::move(reply));});
    Check(future.wait_for(5s)==std::future_status::ready,"native replay transfer timed out");
    return future.get();
  };
  Check(!transfer(provider,{7020,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
      "native upload-before-score staging failed");
  Check(!std::filesystem::is_empty(profile/"community/pending-replays"),"staged replay not durable");
  Check(!transfer(provider,{7020,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
      "repeated native staging failed");
  Check(std::distance(std::filesystem::directory_iterator(profile/"community/pending-replays"),
      std::filesystem::directory_iterator{})==1,"native retry duplicated unbound staging files");
  auto staged_write=write;staged_write.view=7020;staged_write.columns.back().integer=record.size();
  std::promise<uint32_t> staged_done;auto staged_future=staged_done.get_future();
  provider.Write({staged_write},[&](auto error){staged_done.set_value(error);});
  Check(staged_future.wait_for(5s)==std::future_status::ready && !staged_future.get(),"staged native replay did not bind to score");
  Check(std::filesystem::is_empty(profile/"community/pending-replays"),"accepted staged replay retained pending payload");
  auto damaged=record;damaged[0]^=0xff;
  Check(transfer(provider,{7001,0xB13EBABEBABEBABEull,true,uint32_t(damaged.size()),damaged}).error!=0,
      "incompatible native replay uploaded");
  Check(transfer(provider,{7001,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error==0,
      "native replay upload failed");
  // Idempotent retry must not create a second attachment.
  Check(transfer(provider,{7001,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error==0,
      "native replay duplicate rejected");
  auto fetch=[&](FutariCommunity& service,uint32_t view) {
    std::promise<CommunityResult> done;auto future=done.get_future();
    service.Read({1,1,10,{},{{view,{1,2,3,4,5,6,7,8}}}},[&](auto reply){done.set_value(std::move(reply));});
    Check(future.wait_for(5s)==std::future_status::ready,"replay leaderboard read timed out");
    auto reply=future.get();Check(!reply.error && reply.views.size()==1 && !reply.views[0].rows.empty(),"replay leaderboard empty");
    return reply.views[0].rows[0];
  };
  // Independent session and build: the title format, not a host commit hash,
  // determines playback compatibility. Metadata must still match its score.
  FutariCommunity viewer(endpoint,"Ｖｉｅｗｅｒ","newer-host-build",directory/"viewer");
  {
    std::promise<CommunityRankResult> ranks;auto future=ranks.get_future();
    viewer.Estimate({{7001,2500},{7001,2501}},[&](auto result){ranks.set_value(std::move(result));});
    Check(future.wait_for(5s)==std::future_status::ready,"multi-view native rank estimate timed out");
    auto result=future.get();Check(!result.error && result.ranks==std::vector<uint32_t>{2,1},"native rank/tie estimation incorrect");
  }
  auto shared=fetch(viewer,7001);
  Check(shared.xuid!=0xB13EBABEBABEBABEull,"remote replay acquired local native XUID");
  Check(viewer.DisplayName(shared.xuid)=="NativeTest" && viewer.DisplayName(0xB13EBABEBABEBABEull)=="Viewer",
      "cached gamer card lookup lost local or remote Unicode identity");
  {
    const auto previous=GetCommunityService();
    const auto headless=rex::cvar::GetFlagByName("headless");
    struct RestoreCard {
      std::shared_ptr<CommunityService> previous;std::string headless;
      ~RestoreCard(){SetCommunityService(previous);rex::cvar::SetFlagByName("headless",headless);}
    } restore{previous,headless};
    SetCommunityService(std::shared_ptr<CommunityService>(&viewer,[](auto*){}));
    Check(rex::cvar::SetFlagByName("headless","true"),"gamer card headless mode missing");
    PPCContext context{};context.r3.u32=0;context.r4.u64=shared.xuid;
    __imp__XamShowGamerCardUIForXUID(context,nullptr);
    Check(!context.r3.u32,"native gamer card export truncated the 64-bit row token");
    context.r3.u32=0;context.r4.u64=uint32_t(shared.xuid);
    __imp__XamShowGamerCardUIForXUID(context,nullptr);
    Check(context.r3.u32!=0,"unknown short gamer token resolved to another player");
  }
  auto native_size=std::find_if(shared.columns.begin(),shared.columns.end(),[](const auto& c){return c.id==8;});
  Check(native_size!=shared.columns.end() && native_size->type==1 && native_size->integer==record.size(),
      "native leaderboard omitted the accepted replay's actual download size");
  auto native_present=std::find_if(shared.columns.begin(),shared.columns.end(),[](const auto& c){return c.id==1;});
  Check(native_present!=shared.columns.end() && native_present->type==1 && native_present->integer==1,
      "native leaderboard replay-present flag was replaced by the score");
  {
    std::vector<CommunitySpec> specs{{7001,{1,2,3,4,5,6,7,8}}};
    std::vector<uint8_t> data(CommunityStatsBufferSize(specs,1));
    Check(SerializeCommunityStats({0,{{7001,1,{shared}}}},specs,0x40001000,data),"native eight-column replay row serialization failed");
    Check(rex::memory::load_and_swap<uint32_t>(data.data()+72+184)==record.size(),
        "native reader's fixed replay-size offset did not receive attachment metadata");
    Check(rex::memory::load_and_swap<uint32_t>(data.data()+72+16)==1,
        "native Info menu's replay-present offset was not set");
  }
  auto downloaded=transfer(viewer,{7001,shared.xuid,false,uint32_t(record.size()),{}});
  Check(!downloaded.error && downloaded.record==record,"independent replay download changed native record");
  auto staged_shared=fetch(viewer,7020);
  auto staged_download=transfer(viewer,{7020,staged_shared.xuid,false,uint32_t(record.size()),{}});
  Check(!staged_download.error && staged_download.record==record,"upload-before-score published incorrect replay");
  Check(transfer(viewer,{7001,shared.xuid,false,uint32_t(record.size()-1),{}}).error!=0,
      "oversized replay accepted into native buffer");
  Check(transfer(viewer,{7002,shared.xuid,false,uint32_t(record.size()),{}}).error!=0,
      "replay crossed leaderboard modes");
  auto opaque=CommunityReplayPath(7001,shared.xuid);uint32_t path_view=0;uint64_t path_owner=0;
  Check(ParseCommunityReplayPath(opaque,path_view,path_owner) && path_view==7001 && path_owner==shared.xuid,
      "native replay path lost full XUID or view");
  for(auto invalid:{u"https://example.com/replay",u"../../replaydata.bin",u"futari-replay/00001b59/B000000000000001"})
    Check(!ParseCommunityReplayPath(invalid,path_view,path_owner),"noncanonical native replay path accepted");
  for(uint32_t view=7009;view<=7013;++view) {
    auto bad_write=write;bad_write.view=view;
    std::promise<uint32_t> done;auto future=done.get_future();
    provider.Write({bad_write},[&](auto error){done.set_value(error);});
    Check(future.wait_for(5s)==std::future_status::ready && !future.get(),"corruption fixture score failed");
    Check(!transfer(provider,{view,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
        "corruption fixture upload failed");
    auto remote=fetch(viewer,view);
    Check(transfer(viewer,{view,remote.xuid,false,uint32_t(record.size()),{}}).error!=0,
        "tampered replay response accepted");
  }
  viewer.Stop();
  // A new unaccepted score invalidates the old run association immediately.
  std::promise<uint32_t> invalid_run;auto invalid_future=invalid_run.get_future();
  provider.Write({{7001,{{0x20008000,2,42,0}}}},[&](auto error){invalid_run.set_value(error);});
  Check(invalid_future.wait_for(5s)==std::future_status::ready && invalid_future.get()!=0,"invalid new run accepted");
  Check(!transfer(provider,{7001,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
      "new run could not stage its recording");
  auto retry_write=write;retry_write.view=7014;
  std::promise<uint32_t> replay_score;auto replay_score_future=replay_score.get_future();
  provider.Write({retry_write},[&](auto error){replay_score.set_value(error);});
  Check(replay_score_future.wait_for(5s)==std::future_status::ready && !replay_score_future.get(),"replay retry fixture score failed");
  Check(transfer(provider,{7014,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error!=0,
      "temporary replay failure incorrectly reported success");
  Check(!std::filesystem::is_empty(profile/"community/replay-outbox"),"failed replay journal missing");
  provider.Stop();
  {
    FutariCommunity retry(endpoint,"NativeTest","test-build",profile);
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline && !std::filesystem::is_empty(profile/"community/replay-outbox"))
      std::this_thread::sleep_for(20ms);
    retry.Stop();
  }
  Check(std::filesystem::is_empty(profile/"community/replay-outbox"),"queued replay did not retry after restart");
  const auto staged_profile=directory/"staged-retry";
  {
    FutariCommunity staged(endpoint,"StagedRetry","test-build",staged_profile);
    Check(!transfer(staged,{7021,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
        "durable staged retry fixture failed");
    auto run=write;run.view=7021;run.columns.back().integer=record.size();
    std::promise<uint32_t> done;auto future=done.get_future();
    staged.Write({run},[&](auto error){done.set_value(error);});
    Check(future.wait_for(5s)==std::future_status::ready && future.get()!=0,
        "staged replay server failure incorrectly reported success");
    staged.Stop();
  }
  Check(!std::filesystem::is_empty(staged_profile/"community/outbox"),"staged run association not journaled");
  {
    FutariCommunity retry(endpoint,"StagedRetry","test-build",staged_profile);
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline && !std::filesystem::is_empty(staged_profile/"community/outbox"))
      std::this_thread::sleep_for(20ms);
    Check(std::filesystem::is_empty(staged_profile/"community/outbox"),"staged run did not retry after restart");
    auto row=fetch(retry,7021);
    auto replay=transfer(retry,{7021,row.xuid,false,uint32_t(record.size()),{}});
    Check(!replay.error && replay.record==record,"staged retry lost exact run/replay association");
    retry.Stop();
  }
  Check(std::filesystem::is_empty(staged_profile/"community/pending-replays")
      && std::filesystem::is_empty(staged_profile/"community/replay-outbox"),"staged retry left active payloads");

  const auto unbound_profile=directory/"unbound-restart";
  {
    FutariCommunity staged(endpoint,"UnboundTest","test-build",unbound_profile);
    for(uint32_t view=7100;view<7108;++view)
      Check(!transfer(staged,{view,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
          "unbound restart fixture could not fill staging queue");
    staged.Stop();
  }
  {
    FutariCommunity restarted(endpoint,"UnboundTest","test-build",unbound_profile);
    Check(!transfer(restarted,{7108,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error,
        "unbound recordings permanently filled active staging quota after restart");
    size_t archived=0;
    for(const auto& entry:std::filesystem::directory_iterator(unbound_profile/"community/unbound-replays")) {
      const auto body=nlohmann::json::parse(Read(entry.path()));
      Check(!body.contains("score_id") && body.at("sha256").is_string(),"unbound archive lost original metadata");
      ++archived;
    }
    Check(archived==8,"unbound restart discarded recordings instead of retaining them");
    restarted.Stop();
  }
  const auto ambiguous_profile=directory/"ambiguous-restart";
  std::filesystem::create_directories(ambiguous_profile/"community/pending-replays");
  std::filesystem::create_directories(ambiguous_profile/"community/outbox");
  for(const auto& entry:std::filesystem::directory_iterator(unbound_profile/"community/unbound-replays"))
    std::filesystem::copy_file(entry.path(),ambiguous_profile/"community/pending-replays"/entry.path().filename());
  const auto malformed=ambiguous_profile/"community/outbox/0123456789abcdef0123456789abcdef.json";
  {std::ofstream file(malformed);file<<"{\"version\":1,\"scores\":[null]}";}
  {
    FutariCommunity restarted(endpoint,"AmbiguousTest","test-build",ambiguous_profile);
    Check(transfer(restarted,{7109,0xB13EBABEBABEBABEull,true,uint32_t(record.size()),record}).error!=0,
        "malformed score journal did not preserve potentially bound recordings");
    restarted.Stop();
  }
  Check(!std::filesystem::exists(ambiguous_profile/"community/unbound-replays")
      && std::distance(std::filesystem::directory_iterator(ambiguous_profile/"community/pending-replays"),
          std::filesystem::directory_iterator{})==8 && Read(malformed)=="{\"version\":1,\"scores\":[null]}",
      "ambiguous recovery moved or replaced original journals");

  const auto retry_profile=directory/"retry-profile";
  {
    FutariCommunity offline("http://127.0.0.1:1/futari/api/v1","RetryTest","test-build",retry_profile);
    std::promise<uint32_t> failed;auto failed_result=failed.get_future();
    offline.Write({write},[&](auto error){failed.set_value(error);});
    Check(failed_result.wait_for(5s)==std::future_status::ready && failed_result.get()!=0,"offline submit incorrectly reported success");
    offline.Stop();
  }
  Check(std::filesystem::is_directory(retry_profile/"community/outbox") && !std::filesystem::is_empty(retry_profile/"community/outbox"),"offline score journal missing");
  {
    FutariCommunity retry(endpoint,"RetryTest","test-build",retry_profile);
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
      auto history=retry_profile/"community/history";
      if(std::filesystem::is_directory(history) && !std::filesystem::is_empty(history))break;
      std::this_thread::sleep_for(20ms);
    }
    retry.Stop();
  }
  Check(std::filesystem::is_empty(retry_profile/"community/outbox") && !std::filesystem::is_empty(retry_profile/"community/history"),"queued score did not retry after restart");
  Check(FutariNativeNickname("琥珀水母-ABCD").starts_with("Player-"),
      "Unicode nickname was passed to the native Latin-only gamer-tag font");
  Check(FutariNativeNickname("abcdefghijklmnop")=="abcdefghijklmno","native ASCII nickname limit changed");
  const auto alias=FutariNativeNickname("星蝶😀");
  Check(alias.starts_with("Player-") && alias.size()==15 && alias==FutariNativeNickname("星蝶😀"),
      "unrepresentable legacy nickname lacks a stable bounded display alias");
  Check(FutariNormalizeNickname("　ＡＢＣ　")=="ABC" && FutariNormalizeNickname("e\xcc\x81")=="é",
      "client nickname normalization disagrees with server NFKC/trim identity");
  Check(FutariNormalizeNickname("\xff").empty(),"normalization accepted malformed UTF-8");
}
}
int main(int argc, char** argv) {
  try {
    Check(argc == 3 || argc == 4, "Usage: futari_host_tests endpoint test-directory [test-replay]");
    Check(std::string_view(argv[1]).starts_with("http://127.0.0.1:"), "Synthetic tests require an isolated loopback server");
    Check(std::filesystem::is_directory(argv[2]), "test directory missing");
    ConfigTest(argv[2]);
    PadTest();
    ReplayTest();
    ReplayPathAbiTest(argv[1],argv[2]);
    std::vector<uint8_t> record(160);
    auto put=[&](size_t offset,uint32_t value){for(size_t i=0;i<4;++i)record[offset+i]=uint8_t(value>>((3-i)*8));};
    // Synthetic, non-playable XStorage envelope for isolated transfer tests.
    put(0,uint32_t(record.size()));put(4,kFutariReplayVersion);
    if (argc == 4) {
      auto bytes = Read(argv[3]);
      auto data = std::span(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
      auto slots = FutariReadReplaySlots(data);
      Check(!slots.empty(), "real test replay empty");
      auto extracted = FutariExtractReplay(data, slots[0].index);
      Check(FutariReadReplaySlots(extracted).size()==1, "real replay extraction failed");
      std::cout << "PASS: locally recorded replay parsed/extracted, bytes=" << extracted.size() << '\n';
    }
    ProviderTest(argv[1],argv[2],record);
    std::set<std::string> names;
    for (int i=0;i<500;++i) { auto n=FutariRandomNickname(); Check(FutariValidNickname(n), "invalid generated nickname"); names.insert(n); }
    Check(names.size() == 500, "nickname collision in sample");
    for (const auto* bad : {"", " Bob", "Bob ", "\nBob", "a\x01"}) Check(!FutariValidNickname(bad), "invalid nickname accepted");
    FutariLiveClient client(argv[1]);
    auto health = client.Request("/health").get();
    Check(bool(health), "health failed");
    Check(health.body.value("protocol", 0) == 1, "protocol mismatch");
    auto missing = client.Request("/missing").get();
    Check(missing.status == 404 && missing.body.value("error", "") == "not_found", "HTTP error not returned");
    auto invalid = client.Request("/scores", nlohmann::json::object()).get();
    Check(invalid.status == 409, "server validation not propagated");
    std::vector<std::future<FutariHttpResult>> requests;
    for (int i=0;i<64;++i) requests.push_back(client.Request("/health"));
    client.Stop();
    for (auto& future : requests) {
      Check(future.wait_for(2s) == std::future_status::ready, "cancelled promise left unresolved");
      future.get();
    }
    FutariLiveClient offline("http://127.0.0.1:1/futari/api/v1");
    auto start = std::chrono::steady_clock::now();
    Check(!offline.Request("/health").get(), "unreachable endpoint accepted");
    Check(std::chrono::steady_clock::now() - start < 8s, "offline timeout exceeded");
    bool refused=false;
    try { FutariLiveClient insecure("http://example.com/futari/api/v1"); }
    catch (const std::exception&) { refused=true; }
    Check(refused, "non-local plaintext allowed");
    std::cout << "PASS: native config, pad mapping, nickname, HTTP, errors, queue/cancellation, offline\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
