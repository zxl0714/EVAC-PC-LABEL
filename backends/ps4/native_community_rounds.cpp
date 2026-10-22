#include "native_community_rounds.h"
#include "native_np_score.h"
#include "native_user.h"
#include "native_memory.h"
#include "ketsui_version.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))
using namespace kt::community;
struct NativeCommunityRound {
    std::mutex gate;
    RoundInput input;
    bool failed{}, started{};
    RoundSubmission submission;
};
namespace {
constexpr auto& kUpdated=kKetsuiVersions[1];
constexpr std::size_t kFactory=kUpdated.factory,kFactorySlot=kUpdated.factory_slot,kScoreTable=kUpdated.score_table;
constexpr auto kSlots=std::array{kScoreTable+0x40,kScoreTable,kScoreTable+8};
constexpr auto kTargets=kUpdated.job_targets;
constexpr auto kBackendSlots=kUpdated.backend_slots;
constexpr auto kBackendTargets=kUpdated.backend_targets;
constexpr std::size_t kModuleLoaded=kUpdated.module_loaded,kContext=kUpdated.context,kServiceLabel=kUpdated.service_label;
constexpr unsigned char kFactoryPrefix[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
constexpr auto kSource=kUpdated.source;
const KetsuiVersion* active_version{};
using Factory=std::uint64_t(KT_SYSV*)(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
using JobMethod=void(KT_SYSV*)(std::uint64_t);
using BackendInit=std::uint64_t(KT_SYSV*)(std::uint64_t,std::uint64_t);
using BackendGetter=std::uint64_t(KT_SYSV*)(std::uint64_t);
struct PsbValue { std::uint64_t document{},value{}; };
using PsbRoot=PsbValue(KT_SYSV*)(std::uint64_t);
using PsbHas=std::uint64_t(KT_SYSV*)(const PsbValue*,const char*,int);
using PsbItem=PsbValue(KT_SYSV*)(const PsbValue*,const char*);
using PsbInteger=std::uint64_t(KT_SYSV*)(const PsbValue*);
using CopyProfile=void(KT_SYSV*)(std::uint64_t,std::uint64_t);
Factory factory{}; std::array<JobMethod,3> methods{};
BackendInit backend_init{}; BackendGetter backend_getter{};
PsbRoot psb_root{}; PsbHas psb_has{}; PsbItem psb_item{}; PsbInteger psb_integer{}; CopyProfile copy_profile{};
std::uint64_t image_base{};
std::mutex backend_gate;
std::shared_ptr<Client> client;
NativeCommunityRoundHandle invalid;
std::uint64_t score_table{};
struct Binding { std::uint64_t job{}; NativeCommunityRoundHandle round; };
std::array<Binding,128> bindings;
std::mutex binding_gate;
thread_local NativeCommunityRoundHandle current;
void Require(bool value) { if (!value) throw std::runtime_error("Invalid native round adapter input"); }
std::uint64_t Add(std::uint64_t address,std::size_t count) {
    Require(address && count <= (std::numeric_limits<std::uint64_t>::max)()-address); return address+count;
}
void Read(std::uint64_t address,void* target,std::size_t bytes) {
    (void)Add(address,bytes); SIZE_T copied{};
    Require(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),target,bytes,&copied) && copied==bytes);
}
template<class T> T Value(std::uint64_t address,std::size_t offset=0) { T result{}; Read(Add(address,offset),&result,sizeof(result)); return result; }
std::string Text(std::uint64_t object) {
    const auto size=Value<std::uint64_t>(object,0x18),capacity=Value<std::uint64_t>(object,0x20);
    Require(size && size<=2 && size<=capacity && (capacity==15 || capacity>=16));
    const auto source=capacity<16 ? Add(object,8) : Value<std::uint64_t>(object,8);
    std::array<char,3> text{}; Read(source,text.data(),static_cast<std::size_t>(size+1));
    Require(!text[size] && std::all_of(text.begin(),text.begin()+size,[](char c) { return c>='0' && c<='9'; }));
    return {text.data(),static_cast<std::size_t>(size)};
}
NativeCommunityRoundHandle Capture(std::uint64_t boards,std::uint64_t metadata,std::uint64_t stream) {
    Require(active_version);
    auto round=std::make_shared<NativeCommunityRound>();
    const auto begin=Value<std::uint64_t>(boards,8),end=Value<std::uint64_t>(boards,16);
    Require(begin && end>=begin && (end-begin)%40==0 && (end-begin)/40>=1 && (end-begin)/40<=18);
    for (auto item=begin;item<end;item=Add(item,40)) {
        const auto text=Text(item); const auto board=static_cast<std::uint32_t>(Decimal(text,62));
        Require(board%10<=2 && std::find(round->input.boards.begin(),round->input.boards.end(),board)==round->input.boards.end());
        round->input.boards.push_back(board);
    }
    const auto score=Value<std::int64_t>(metadata,0x38); Require(score>=0); round->input.score=static_cast<std::uint64_t>(score);
    Require(Value<std::uint32_t>(stream,0x68)==kReplayRecordSize);
    round->input.record.resize(kReplayRecordSize); Read(Value<std::uint64_t>(stream,0x60),round->input.record.data(),round->input.record.size());
    std::uint32_t version{},data_version{},record_score{};
    std::memcpy(&version,round->input.record.data(),4); std::memcpy(&data_version,round->input.record.data()+4,4); std::memcpy(&record_score,round->input.record.data()+108,4);
    if (version!=0x10001 || data_version!=active_version->replay_data_version || round->input.score!=record_score) {
        std::printf("{\"event\":\"native_community_round_header_rejected\",\"container_version\":%u,\"data_version\":%u,\"expected_data_version\":%u,\"score_matches\":%s}\n",
                    version,data_version,active_version->replay_data_version,round->input.score==record_score?"true":"false");
        Require(false);
    }
    round->input.build="ketsui-native-round-"+std::string(active_version->version.substr(1)); return round;
}
NativeCommunityRoundHandle For(std::uint64_t job) {
    std::lock_guard lock(binding_gate);
    for (const auto& b:bindings) if (b.job==job) return b.round;
    return invalid;
}
void Erase(std::uint64_t job) {
    std::lock_guard lock(binding_gate);
    for (auto& b:bindings) if (b.job==job) { b={}; return; }
}
void Register(std::uint64_t group,const NativeCommunityRoundHandle& round) {
    std::array<std::uint64_t,18> jobs{}; std::size_t count{};
    auto job=Value<std::uint64_t>(group,0x28);
    while (job) {
        Require(count<jobs.size() && Value<std::uint64_t>(job)==score_table &&
                std::find(jobs.begin(),jobs.begin()+count,job)==jobs.begin()+count);
        jobs[count++]=job; job=Value<std::uint64_t>(job,0x10);
    }
    Require(count && (round->failed || count==round->input.boards.size()));
    std::lock_guard lock(binding_gate);
    bool collision{};
    for (std::size_t i=0;i<count;++i) for (auto& old:bindings) if (old.job==jobs[i]) { old.round=invalid; collision=true; }
    Require(!collision);
    Require(static_cast<std::size_t>(std::count_if(bindings.begin(),bindings.end(),[](const auto& b) { return !b.job; }))>=count);
    for (std::size_t i=0;i<count;++i) {
        const auto slot=std::find_if(bindings.begin(),bindings.end(),[](const auto& b) { return !b.job; }); *slot={jobs[i],round};
    }
}
struct Errors { DWORD windows=GetLastError(); int crt=errno; void Restore() const { errno=crt; SetLastError(windows); } };
bool BindBackendIdentity(std::uint64_t backend,const Identity& identity) {
    if (!identity.ready) return true;
    Require(identity.account && ValidNicknameBytes(identity.nickname) && identity.account==AccountId(identity.nickname));
    // An existing local backend may become online later. This is the verified
    // community identity, not an anonymous value returned by the NP API.
    if (Value<std::uint64_t>(backend,0xa8) && Value<std::uint64_t>(backend,0xb8)!=identity.account)
        Require(CopyToNativeMemory(reinterpret_cast<void*>(Add(backend,0xb8)),&identity.account,8));
    return true;
}
__declspec(noinline) std::uint64_t BackendInitializedHost(std::uint64_t backend) {
    const Errors incoming;
    try { BindBackendIdentity(backend,client->State()); }
    catch (...) { incoming.Restore(); return 0; }
    incoming.Restore(); return backend_getter(backend);
}
std::uint64_t KT_SYSV BackendInitialized(std::uint64_t backend) { return BackendInitializedHost(backend); }
// Windows exception handling and lease destruction stay in a host-ABI frame.
// The game-facing adapters below only translate the calling convention.
__declspec(noinline) std::uint64_t InitializeBackendHost(std::uint64_t backend,std::uint64_t config) {
    const Errors incoming;
    std::lock_guard lock(backend_gate);
    incoming.Restore();
    const auto result=backend_init(backend,config);
    const Errors returned;
    if (result) { returned.Restore(); return result; }
    bool initialized{}; int created{}; const char* stage="offline_or_module_gate";
    try {
        // The normal initializer remains authoritative online. Its module
        // loading also precedes the account query, so a missing module is not
        // turned into successful local initialization.
        if (!client->State().ready && NativeUserInitialized() && Value<unsigned char>(image_base,active_version->module_loaded)==1) {
            stage="profile_label";
            const auto root=psb_root(config);
            // The verified original call tests AL. Upper RAX bits are not a
            // boolean result and may remain nonzero when the label is absent.
            Require(static_cast<unsigned char>(psb_has(&root,"service_label",0))!=0);
            const auto label=psb_item(&root,"service_label");
            Require(psb_integer(&label)==0);
            const auto previous_label=Value<std::int32_t>(image_base,active_version->service_label);
            Require(previous_label==-1 || previous_label==0);
            auto context=Value<int>(image_base,active_version->context);
            stage="write_leases";
            auto config_write=BeginNativeHostWrite(reinterpret_cast<void*>(Add(backend,0xa8)),8);
            auto account_write=BeginNativeHostWrite(reinterpret_cast<void*>(Add(backend,0xb8)),8);
            auto label_write=BeginNativeHostWrite(reinterpret_cast<void*>(Add(image_base,active_version->service_label)),4);
            auto context_write=BeginNativeHostWrite(reinterpret_cast<void*>(Add(image_base,active_version->context)),4);
            Require(config_write && account_write && label_write && context_write);
            if (context<=0) {
                stage="local_context";
                using CreateContext=int(KT_SYSV*)(unsigned,int);
                const auto create=reinterpret_cast<CreateContext>(ResolveScoreImport("GWnWQNXZH5M","libSceNpScore","libSceNpScore"));
                Require(create); context=create(0,0x1000); Require(context>0); created=context;
            }
            // Keep the original profile copy, column encoder and task queue.
            // A zero here is an unbound backend field, not a claimed account.
            stage="profile_copy"; copy_profile(Add(backend,0xa8),config);
            Require(Value<std::uint64_t>(backend,0xa8)!=0);
            const std::uint64_t unbound{}; const int zero{};
            std::memcpy(reinterpret_cast<void*>(Add(backend,0xb8)),&unbound,8);
            std::memcpy(reinterpret_cast<void*>(Add(image_base,active_version->service_label)),&zero,4);
            std::memcpy(reinterpret_cast<void*>(Add(image_base,active_version->context)),&context,4);
            initialized=true; created=0; stage="ready";
        }
    } catch (...) {}
    if (created>0) {
        using DeleteContext=int(KT_SYSV*)(int);
        const auto remove=reinterpret_cast<DeleteContext>(ResolveScoreImport("G0pE+RNCwfk","libSceNpScore","libSceNpScore"));
        if (remove) (void)remove(created);
    }
    std::printf("{\"event\":\"native_community_backend_local_init\",\"initialized\":%s,\"stage\":\"%s\",\"online_identity_claimed\":false}\n",initialized?"true":"false",stage);
    returned.Restore(); return initialized ? 1 : result;
}
std::uint64_t KT_SYSV InitializeBackend(std::uint64_t backend,std::uint64_t config) { return InitializeBackendHost(backend,config); }
void KT_SYSV Tick(std::uint64_t job) {
    const Errors errors;
    auto previous=std::move(current);
    try { current=For(job); } catch (...) { current=invalid; }
    errors.Restore(); methods[0](job); const Errors returned; current=std::move(previous); returned.Restore();
}
void KT_SYSV Destroy(std::uint64_t job) { const Errors errors; try { Erase(job); } catch (...) {} errors.Restore(); methods[1](job); }
void KT_SYSV Delete(std::uint64_t job) { const Errors errors; try { Erase(job); } catch (...) {} errors.Restore(); methods[2](job); }
std::uint64_t KT_SYSV Create(std::uint64_t backend,std::uint64_t boards,std::uint64_t metadata,std::uint64_t stream,std::uint64_t owner) {
    const Errors errors; auto round=invalid;
    try { round=Capture(boards,metadata,stream); } catch (...) {}
    errors.Restore(); const auto group=factory(backend,boards,metadata,stream,owner);
    const Errors returned;
    try { Register(group,round); std::printf("{\"event\":\"native_community_round_capture\",\"valid\":%s,\"boards\":%zu}\n",round->failed?"false":"true",round->input.boards.size()); }
    catch (...) { std::puts("{\"event\":\"native_community_round_capture\",\"valid\":false,\"binding_failed\":true}"); }
    returned.Restore(); return group;
}
Reply Failure(Error error) { Reply reply; reply.error=error; return reply; }
}
bool ConfigureNativeCommunityRounds(std::shared_ptr<Client> value) {
    if (client || !value) return false;
    invalid=std::make_shared<NativeCommunityRound>(); invalid->failed=true; client=std::move(value); return true;
}
bool NativeCommunityRoundsEnabled() { return bool(client); }
NativeCommunityRoundHandle CurrentNativeCommunityRound() { return current; }
void InstallNativeCommunityRounds(unsigned char* image,std::size_t bytes,std::span<const std::uint8_t,32> source) {
    if (!client) return;
    const auto* version=FindKetsuiVersion(source);
    Require(!factory && image && version);
    const auto& v=*version;const auto slots=v.JobSlots();
    const auto inside=[&](std::size_t offset,std::size_t extent) {return offset<=bytes && extent<=bytes-offset;};
    for(auto offset:{v.module_loaded,v.context,v.service_label,v.factory_slot,v.score_table,
                    v.psb_root,v.psb_has,v.psb_item,v.psb_integer,v.copy_profile})Require(inside(offset,8));
    Require(inside(v.factory,sizeof(kFactoryPrefix)) && !std::memcmp(image+v.factory,kFactoryPrefix,sizeof(kFactoryPrefix)) &&
            Value<std::uint64_t>(reinterpret_cast<std::uint64_t>(image),v.factory_slot)==reinterpret_cast<std::uint64_t>(image+v.factory));
    for (std::size_t i=0;i<slots.size();++i) {
        Require(inside(slots[i],8) && inside(v.job_targets[i],1));
        Require(Value<std::uint64_t>(reinterpret_cast<std::uint64_t>(image),slots[i])==reinterpret_cast<std::uint64_t>(image+v.job_targets[i]));
    }
    for (std::size_t i=0;i<v.backend_slots.size();++i) {
        Require(inside(v.backend_slots[i],8) && inside(v.backend_targets[i],1));
        Require(Value<std::uint64_t>(reinterpret_cast<std::uint64_t>(image),v.backend_slots[i])==reinterpret_cast<std::uint64_t>(image+v.backend_targets[i]));
    }
    active_version=version;
    factory=reinterpret_cast<Factory>(image+v.factory); score_table=reinterpret_cast<std::uint64_t>(image+v.score_table);
    image_base=reinterpret_cast<std::uint64_t>(image);
    backend_init=reinterpret_cast<BackendInit>(image+v.backend_targets[0]); backend_getter=reinterpret_cast<BackendGetter>(image+v.backend_targets[1]);
    psb_root=reinterpret_cast<PsbRoot>(image+v.psb_root); psb_has=reinterpret_cast<PsbHas>(image+v.psb_has);
    psb_item=reinterpret_cast<PsbItem>(image+v.psb_item); psb_integer=reinterpret_cast<PsbInteger>(image+v.psb_integer);
    copy_profile=reinterpret_cast<CopyProfile>(image+v.copy_profile);
    for (std::size_t i=0;i<methods.size();++i) methods[i]=reinterpret_cast<JobMethod>(image+v.job_targets[i]);
    const auto write=[&](std::size_t slot,auto function) { const auto target=reinterpret_cast<std::uint64_t>(function); std::memcpy(image+slot,&target,8); };
    write(v.factory_slot,&Create); write(slots[0],&Tick); write(slots[1],&Destroy); write(slots[2],&Delete);
    write(v.backend_slots[0],&InitializeBackend); write(v.backend_slots[1],&BackendInitialized);
    std::printf("{\"event\":\"native_community_round_producer_installed\",\"source_bound\":true,\"data_version\":\"%.*s\"}\n",
                static_cast<int>(v.version.size()),v.version.data());
}
bool BeginNativeCommunityRoundScore(const NativeCommunityRoundHandle& round,std::uint32_t board,std::uint64_t score,
                                   std::span<const std::byte,64> comment,std::span<const std::byte> info) {
    if (!round || round->failed || !client) return false;
    std::lock_guard lock(round->gate);
    if (score!=round->input.score || std::find(round->input.boards.begin(),round->input.boards.end(),board)==round->input.boards.end() || info.size()>189) return false;
    if (round->started) return std::equal(comment.begin(),comment.end(),round->input.comment.begin()) && std::equal(info.begin(),info.end(),round->input.game_info.begin(),round->input.game_info.end());
    std::copy(comment.begin(),comment.end(),round->input.comment.begin()); round->input.game_info.assign(info.begin(),info.end());
    // Keep only the small immutable signature in this task binding. The client
    // worker receives the sole record vector; no guest pointer leaves here.
    RoundInput input; input.boards=round->input.boards; input.score=round->input.score; input.comment=round->input.comment;
    input.game_info=round->input.game_info; input.build=round->input.build; input.record=std::move(round->input.record);
    round->submission=client->SubmitRound(std::move(input)); round->started=true; return true;
}
NativeCommunityRoundReply PollNativeCommunityRoundScore(const NativeCommunityRoundHandle& round,std::uint32_t board) {
    if (!round || round->failed) return {true,Failure(Error::Protocol),{}, {}};
    std::lock_guard lock(round->gate); if (!round->started) return {true,Failure(Error::Protocol),{}, {}};
    if (round->submission.accepted.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
    const auto& accepted=round->submission.accepted.get();
    if (!accepted.durable || accepted.error!=Error::None) return {true,Failure(accepted.error),{}, {}};
    if (round->submission.first_attempt.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
    const auto& attempted=round->submission.first_attempt.get();
    const auto ref=std::find_if(accepted.submissions.begin(),accepted.submissions.end(),[&](const auto& r) { return r.first==board; });
    const auto result=std::find_if(attempted.boards.begin(),attempted.boards.end(),[&](const auto& r) { return r.board==board; });
    if (accepted.reference!=attempted.reference || ref==accepted.submissions.end() || result==attempted.boards.end()) return {true,Failure(Error::Protocol),{}, {}};
    return {true,result->score ? *result->score : Failure(attempted.error==Error::None?Error::Protocol:attempted.error),ref->second,attempted.identity};
}
std::future<Reply> ProveNativeCommunityRoundReplay(const NativeCommunityRoundHandle& round,std::uint32_t board,std::uint64_t score,std::vector<std::byte> payload) {
    Require(round && !round->failed && round->started && client);
    return client->ProveRoundReplay(round->submission,board,score,std::move(payload));
}
std::size_t NativeCommunityRoundReplayBytes(const NativeCommunityRoundHandle& round) {
    Require(round && !round->failed && round->started && round->submission.accepted.wait_for(std::chrono::seconds(0))==std::future_status::ready);
    return round->submission.accepted.get().attachment_bytes;
}
namespace {
std::uint64_t test_group{};
std::array<std::uint64_t,5> test_arguments{};
NativeCommunityRoundHandle test_seen;
unsigned test_destroyed{};
std::uint64_t test_init_result{}; bool test_module_loaded=true,test_copy_failure{};
unsigned test_copied{}; std::array<std::uint64_t,2> test_init_arguments{};
struct TestProfile { unsigned label{}; bool present=true; };
std::uint64_t KT_SYSV TestBackendInit(std::uint64_t backend,std::uint64_t config) {
    test_init_arguments={backend,config}; const auto loaded=static_cast<unsigned char>(test_module_loaded);
    std::memcpy(reinterpret_cast<void*>(image_base+kModuleLoaded),&loaded,1);
    errno=ERANGE; SetLastError(103); return test_init_result;
}
std::uint64_t KT_SYSV TestBackendGetter(std::uint64_t backend) { const auto result=Value<std::uint64_t>(backend,0xa8)!=0; errno=EDOM; SetLastError(127); return result; }
PsbValue KT_SYSV TestPsbRoot(std::uint64_t config) { return {config,0x51}; }
std::uint64_t KT_SYSV TestPsbHas(const PsbValue* value,const char* key,int mode) {
    const auto present=value->value==0x51 && std::strcmp(key,"service_label")==0 && !mode &&
                       reinterpret_cast<const TestProfile*>(value->document)->present;
    // The original caller checks AL, not the unspecified upper return bits.
    return 0x7a13000000000000ULL | static_cast<unsigned>(present);
}
PsbValue KT_SYSV TestPsbItem(const PsbValue* value,const char* key) { return std::strcmp(key,"service_label")==0 ? PsbValue{value->document,0x52} : PsbValue{}; }
std::uint64_t KT_SYSV TestPsbInteger(const PsbValue* value) { return value->value==0x52 ? reinterpret_cast<const TestProfile*>(value->document)->label : UINT64_MAX; }
void KT_SYSV TestCopyProfile(std::uint64_t target,std::uint64_t config) {
    // Do not throw a C++ exception across a SysV callback. The adapter checks
    // the returned profile state after the original callback has returned.
    if (test_copy_failure || config!=test_init_arguments[1]) return;
    ++test_copied;
    const std::uint64_t marker=0x600; std::memcpy(reinterpret_cast<void*>(target),&marker,8); errno=EDOM; SetLastError(117);
}
std::uint64_t KT_SYSV TestFactory(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d,std::uint64_t e) {
    test_arguments={a,b,c,d,e}; errno=ERANGE; SetLastError(103); return test_group;
}
void KT_SYSV TestTick(std::uint64_t) { test_seen=CurrentNativeCommunityRound(); errno=EDOM; SetLastError(117); }
void KT_SYSV TestDestroy(std::uint64_t) { ++test_destroyed; }
template<class T> void Put(void* address,std::size_t offset,T value) { std::memcpy(static_cast<std::byte*>(address)+offset,&value,sizeof(value)); }
}
bool NativeCommunityRoundsSelfTest(const std::filesystem::path& directory) {
    Require(!client && !factory && directory.is_absolute() && !std::filesystem::exists(directory));
    unsigned cases{}; bool okay=true;
    const auto check=[&](bool value,const char* name) { ++cases; okay&=value; std::printf("{\"event\":\"native_round_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,value?"true":"false"); };
    const auto rejects=[](auto operation) { try { operation(); return false; } catch (...) { return true; } };
    std::array<std::uint8_t,32> source{}; const auto nibble=[](char c) { return c<='9'?c-'0':c-'a'+10; };
    for (std::size_t i=0;i<source.size();++i) source[i]=static_cast<std::uint8_t>(16*nibble(kSource[2*i])+nibble(kSource[2*i+1]));
    InstallNativeCommunityRounds(nullptr,0,source); check(!factory && !NativeCommunityRoundsEnabled(),"ordinary_disabled_load_never_inspects_an_image");
    Config config; config.enabled=false;
    auto disabled=std::make_shared<Client>(config,directory,true);
    check(ConfigureNativeCommunityRounds(disabled) && !ConfigureNativeCommunityRounds(disabled),"producer_is_startup_only_and_configured_once");
    check(!FindKetsuiVersion(std::string_view{}) && !FindKetsuiVersion(std::string(64,'0')),"unknown_or_empty_version_identity_is_not_inferred_from_offsets");
    for(const auto& version:kKetsuiVersions) {
        std::vector<unsigned char> fixture(version.context+8);
        const auto address=reinterpret_cast<std::uint64_t>(fixture.data());
        const auto slots=version.JobSlots();
        std::array<std::uint8_t,32> identity{};
        for(std::size_t i=0;i<identity.size();++i)identity[i]=static_cast<std::uint8_t>(16*nibble(version.source[2*i])+nibble(version.source[2*i+1]));
        std::memcpy(fixture.data()+version.factory,kFactoryPrefix,sizeof(kFactoryPrefix));
        Put(fixture.data(),version.factory_slot,address+version.factory);
        for(std::size_t i=0;i<slots.size();++i)Put(fixture.data(),slots[i],address+version.job_targets[i]);
        for(std::size_t i=0;i<version.backend_slots.size();++i)Put(fixture.data(),version.backend_slots[i],address+version.backend_targets[i]);
        auto other=identity;other[0]^=1;
        check(rejects([&] {InstallNativeCommunityRounds(fixture.data(),fixture.size(),other);}) && !factory &&
              Value<std::uint64_t>(address,version.factory_slot)==address+version.factory,"each_version_rejects_unknown_hash_without_writes");
        const auto& peer=kKetsuiVersions[version.version=="01.00"?1:0];
        for(std::size_t i=0;i<other.size();++i)other[i]=static_cast<std::uint8_t>(16*nibble(peer.source[2*i])+nibble(peer.source[2*i+1]));
        check(rejects([&] {InstallNativeCommunityRounds(fixture.data(),fixture.size(),other);}) && !factory &&
              Value<std::uint64_t>(address,version.factory_slot)==address+version.factory,"a_known_peer_hash_cannot_select_the_other_versions_slots");
        check(rejects([&] {InstallNativeCommunityRounds(fixture.data(),version.factory_slot+7,identity);}) && !factory,
              "each_version_checks_image_extent_before_slot_reads");
        InstallNativeCommunityRounds(fixture.data(),fixture.size(),identity);
        check(active_version==&version && factory==reinterpret_cast<Factory>(fixture.data()+version.factory) &&
              score_table==address+version.score_table && psb_item==reinterpret_cast<PsbItem>(fixture.data()+version.psb_item) &&
              Value<std::uint64_t>(address,version.factory_slot)==reinterpret_cast<std::uint64_t>(&Create) &&
              Value<std::uint64_t>(address,slots[0])==reinterpret_cast<std::uint64_t>(&Tick),
              "each_known_version_installs_only_its_verified_code_and_dispatch_sites");
        factory=nullptr;methods={};score_table=0;backend_init=nullptr;backend_getter=nullptr;
        psb_root=nullptr;psb_has=nullptr;psb_item=nullptr;psb_integer=nullptr;copy_profile=nullptr;image_base=0;active_version=nullptr;
    }
    std::vector<unsigned char> image(kContext+8);
    std::memcpy(image.data()+kFactory,kFactoryPrefix,sizeof(kFactoryPrefix));
    const auto base=reinterpret_cast<std::uint64_t>(image.data()); Put(image.data(),kFactorySlot,base+kFactory);
    for (std::size_t i=0;i<kSlots.size();++i) Put(image.data(),kSlots[i],base+kTargets[i]);
    for (std::size_t i=0;i<kBackendSlots.size();++i) Put(image.data(),kBackendSlots[i],base+kBackendTargets[i]);
    source[0]^=1; check(rejects([&] { InstallNativeCommunityRounds(image.data(),image.size(),source); }) && !factory,"wrong_source_identity_rejected_before_any_slot_write"); source[0]^=1;
    Put(image.data(),kSlots[2],std::uint64_t{3});
    check(rejects([&] { InstallNativeCommunityRounds(image.data(),image.size(),source); }) && Value<std::uint64_t>(base,kFactorySlot)==base+kFactory,"all_dispatch_targets_are_checked_before_patching"); Put(image.data(),kSlots[2],base+kTargets[2]);
    Put(image.data(),kBackendSlots[1],std::uint64_t{3});
    check(rejects([&] { InstallNativeCommunityRounds(image.data(),image.size(),source); }) && Value<std::uint64_t>(base,kFactorySlot)==base+kFactory,
          "local_backend_targets_are_also_checked_before_any_slot_write"); Put(image.data(),kBackendSlots[1],base+kBackendTargets[1]);
    InstallNativeCommunityRounds(image.data(),image.size(),source);
    check(Value<std::uint64_t>(base,kFactorySlot)==reinterpret_cast<std::uint64_t>(&Create) && Value<std::uint64_t>(base,kSlots[0])==reinterpret_cast<std::uint64_t>(&Tick),"known_version_factory_and_job_slots_are_redirected");
    factory=&TestFactory; methods={&TestTick,&TestDestroy,&TestDestroy};
    std::array<std::byte,80> names{};
    for (unsigned i=0;i<2;++i) { names[i*40+8]=static_cast<std::byte>('0'+i); Put(names.data(),i*40+24,std::uint64_t{1}); Put(names.data(),i*40+32,std::uint64_t{15}); }
    std::array<std::uint64_t,3> board_vector{0,reinterpret_cast<std::uint64_t>(names.data()),reinterpret_cast<std::uint64_t>(names.data()+names.size())};
    std::array<std::byte,0x90> metadata{}; Put(metadata.data(),0x38,std::int64_t{9001});
    std::vector<std::byte> raw(kReplayRecordSize); Put(raw.data(),0,std::uint32_t{0x10001}); Put(raw.data(),4,std::uint32_t{0x10001}); Put(raw.data(),108,std::uint32_t{9001});
    std::array<std::byte,0x70> stream{}; Put(stream.data(),0x60,reinterpret_cast<std::uint64_t>(raw.data())); Put(stream.data(),0x68,std::uint32_t{kReplayRecordSize});
    std::array<std::byte,0x168> first{},second{}; const auto one=reinterpret_cast<std::uint64_t>(first.data()),two=reinterpret_cast<std::uint64_t>(second.data());
    Put(first.data(),0,score_table); Put(second.data(),0,score_table); Put(first.data(),0x10,two);
    std::array<std::byte,0x40> group{}; Put(group.data(),0x28,one); test_group=reinterpret_cast<std::uint64_t>(group.data());
    const std::array<std::uint64_t,5> args{71,reinterpret_cast<std::uint64_t>(board_vector.data()),reinterpret_cast<std::uint64_t>(metadata.data()),reinterpret_cast<std::uint64_t>(stream.data()),83};
    const auto returned=Create(args[0],args[1],args[2],args[3],args[4]);
    check(returned==test_group && test_arguments==args && errno==ERANGE && GetLastError()==103,"factory_forwards_all_five_sysv_arguments_return_and_error_state");
    const auto a=For(one); check(a && !a->failed && For(two)==a && a->input.boards==std::vector<std::uint32_t>({0,1}),"each_created_job_is_bound_to_its_shared_round_object");
    raw[300]=std::byte{7}; check(a->input.record[300]==std::byte{} && a->input.record.size()==kReplayRecordSize,"capture_is_owned_not_a_borrowed_guest_record");
    {
        const auto* saved_version=active_version;
        for(const auto& version:kKetsuiVersions) {
            active_version=&version;
            Put(raw.data(),4,version.replay_data_version);
            const auto captured=Capture(args[1],args[2],args[3]);
            check(captured && captured->input.record==raw && captured->input.build=="ketsui-native-round-"+std::string(version.version.substr(1)),
                  "each_verified_executable_captures_its_native_replay_revision_without_rewriting");
            Put(raw.data(),4,version.replay_data_version==0x10000?std::uint32_t{0x10001}:std::uint32_t{0x10000});
            check(rejects([&] {Capture(args[1],args[2],args[3]);}),"producer_rejects_the_other_executables_record_revision");
        }
        active_version=saved_version;Put(raw.data(),4,saved_version->replay_data_version);
    }
    {
        for (unsigned i=0;i<2;++i) {
            names[i*40+8]=std::byte{'6'};names[i*40+9]=static_cast<std::byte>('0'+2*i);
            names[i*40+10]=std::byte{};Put(names.data(),i*40+24,std::uint64_t{2});
        }
        const auto custom=Capture(args[1],args[2],args[3]);
        check(custom && custom->input.boards==std::vector<std::uint32_t>({60,62}) && custom->input.record==raw,
            "custom_native_board_names_and_original_owned_record_are_captured");
        for (unsigned i=0;i<2;++i) {
            names[i*40+8]=static_cast<std::byte>('0'+i);names[i*40+9]=std::byte{};
            Put(names.data(),i*40+24,std::uint64_t{1});
        }
    }
    Tick(one); check(test_seen==a && !CurrentNativeCommunityRound() && errno==EDOM && GetLastError()==117,"job_scope_is_exact_and_restored_after_the_original_tick");
    Tick(19); check(test_seen==invalid && !CurrentNativeCommunityRound(),"unregistered_jobs_cannot_inherit_a_previous_round_scope");
    std::array<std::byte,64> comment{}; const std::array<std::byte,2> info{std::byte{1},std::byte{2}};
    check(BeginNativeCommunityRoundScore(a,0,9001,comment,info) && BeginNativeCommunityRoundScore(a,1,9001,comment,info) && a->input.record.empty(),"one_round_submission_owns_both_jobs_and_consumes_the_record_once");
    auto changed=comment; changed[0]=std::byte{1};
    check(!BeginNativeCommunityRoundScore(a,0,9001,changed,info) && !BeginNativeCommunityRoundScore(a,2,9001,comment,info) &&
          !BeginNativeCommunityRoundScore(a,0,9002,comment,info),"mismatched_board_score_or_native_metadata_never_enqueues_another_submission");
    const auto disabled_reply=PollNativeCommunityRoundScore(a,0);
    check(disabled_reply.ready && disabled_reply.reply.error==Error::Disabled && disabled_reply.submission.empty(),"local_rejection_is_not_fabricated_as_a_server_receipt");
    Destroy(one); Delete(two); check(For(one)==invalid && For(two)==invalid && test_destroyed==2,"both_destructor_paths_remove_job_bindings");
    Create(args[0],args[1],args[2],args[3],args[4]); const auto b=For(one);
    check(b && b!=a && !b->failed,"identical_score_and_record_in_another_batch_get_a_distinct_round_object");
    using Initialize=int(KT_SYSV*)(const void*);
    using Context=int(KT_SYSV*)(unsigned,int);
    using OneId=int(KT_SYSV*)(int);
    using Score=int(KT_SYSV*)(int,unsigned,std::int64_t,const void*,const void*,unsigned*,const void*,void*);
    using Poll=int(KT_SYSV*)(int,int*);
    using Range=int(KT_SYSV*)(int,unsigned,unsigned,void*,std::size_t,void*,std::size_t,void*,std::size_t,unsigned,void*,void*,void*);
    const auto api=[](const char* id) { return ResolveScoreImport(id,"libSceNpScore","libSceNpScore"); };
    const auto initialize=reinterpret_cast<Initialize>(ResolveUserImport("j3YMu1MVNNo","libSceUserService","libSceUserService"));
    Require(initialize && initialize(nullptr)==0 && ConfigureNativeScore(disabled));
    const auto context=reinterpret_cast<Context>(api("GWnWQNXZH5M"))(0,0x1000);
    const auto request=reinterpret_cast<OneId>(api("gW8qyjYrUbk"))(context);
    check(context>0 && request>0 && !disabled->State().ready && !disabled->State().account,
          "explicit_rounds_can_create_local_handles_without_claiming_an_online_identity");
    std::array<std::byte,144> offline_ranks; offline_ranks.fill(std::byte{0x47});
    check(reinterpret_cast<Range>(api("y5ja7WI05rs"))(request,0,1,offline_ranks.data(),offline_ranks.size(),nullptr,0,nullptr,0,1,nullptr,nullptr,nullptr)<0 &&
          std::all_of(offline_ranks.begin(),offline_ranks.end(),[](auto value) { return value==std::byte{0x47}; }),
          "local_offline_handles_do_not_turn_ranking_failure_into_an_empty_success");
    const auto score_api=reinterpret_cast<Score>(api("ANJssPz3mY0"));
    unsigned rank=73; current=b;
    const auto started=score_api(request,0,9001,nullptr,nullptr,&rank,nullptr,nullptr); current.reset();
    int result=91; const auto polled=reinterpret_cast<Poll>(api("m1DfNRstkSQ"))(request,&result);
    check(started==0 && polled==0 && result<0 && rank==73 && !disabled->State().ready && !disabled->State().account &&
          score_api(request,0,9001,nullptr,nullptr,nullptr,nullptr,nullptr)<0,
          "scoped_offline_submission_reaches_local_acceptance_but_rejection_is_not_server_success");
    Require(reinterpret_cast<OneId>(api("dK8-SgYf6r4"))(request)==0 && reinterpret_cast<OneId>(api("G0pE+RNCwfk"))(context)==0);
    backend_init=&TestBackendInit; backend_getter=&TestBackendGetter;
    psb_root=&TestPsbRoot; psb_has=&TestPsbHas; psb_item=&TestPsbItem; psb_integer=&TestPsbInteger; copy_profile=&TestCopyProfile;
    Put(image.data(),kServiceLabel,std::int32_t{-1});
    std::array<std::byte,0xc0> local_backend{}; const auto backend=reinterpret_cast<std::uint64_t>(local_backend.data());
    TestProfile profile; const auto profile_address=reinterpret_cast<std::uint64_t>(&profile);
    profile.label=1;
    check(!InitializeBackend(backend,profile_address) && !test_copied && !NativeScoreContextsLive(),"local_backend_rejects_an_unsupported_service_label");
    profile.label=0; profile.present=false;
    check(!InitializeBackend(backend,profile_address) && !test_copied && !NativeScoreContextsLive(),"local_backend_requires_the_explicit_profile_label");
    profile.present=true; test_module_loaded=false;
    check(!InitializeBackend(backend,profile_address) && !test_copied && !NativeScoreContextsLive(),"a_failed_native_module_load_is_not_replaced_by_local_success");
    test_module_loaded=true;
    check(!InitializeBackend(0,profile_address) && !test_copied && !NativeScoreContextsLive(),"all_backend_write_leases_precede_local_context_creation");
    test_copy_failure=true;
    check(!InitializeBackend(backend,profile_address) && !NativeScoreContextsLive() && !Value<int>(base,kContext),"missing_profile_copy_releases_its_unpublished_local_context");
    test_copy_failure=false;
    check(InitializeBackend(backend,profile_address)==1 && test_init_arguments==std::array<std::uint64_t,2>{backend,profile_address} &&
          test_copied==1 && Value<int>(base,kContext)>0 && Value<int>(base,kServiceLabel)==0 && Value<std::uint64_t>(backend,0xa8)==0x600 &&
          !Value<std::uint64_t>(backend,0xb8) && !disabled->State().ready && !disabled->State().account && errno==ERANGE && GetLastError()==103,
          "offline_backend_keeps_original_profile_copy_and_an_unbound_account_without_claiming_online");
    const auto local_context=Value<int>(base,kContext); test_init_result=1;
    check(InitializeBackend(backend,profile_address)==1 && test_copied==1 && Value<int>(base,kContext)==local_context,
          "successful_original_initialization_is_forwarded_without_duplicate_local_work");
    const Identity verified{true,"Alice",AccountId("Alice")};
    check(BindBackendIdentity(backend,verified) && Value<std::uint64_t>(backend,0xb8)==verified.account,"an_initialized_backend_can_receive_its_verified_community_identity");
    check(BindBackendIdentity(backend,Identity{}) && Value<std::uint64_t>(backend,0xb8)==verified.account,"going_offline_does_not_manufacture_or_rebind_an_account");
    check(rejects([&] { BindBackendIdentity(backend,{true,"Alice",7}); }) && Value<std::uint64_t>(backend,0xb8)==verified.account,
          "identity_mismatch_is_rejected_before_a_backend_write");
    check(BackendInitialized(backend)==1 && errno==EDOM && GetLastError()==127,"backend_getter_keeps_the_original_local_state_and_error_contract");
    check(reinterpret_cast<OneId>(api("G0pE+RNCwfk"))(local_context)==0 && !NativeScoreContextsLive(),"the_original_context_teardown_api_releases_the_local_backend_context");
    Put(image.data(),kContext,0); test_init_result=0;
    Create(args[0],args[1],args[2],args[3],args[4]); check(For(one)==invalid && For(two)==invalid,"address_reuse_without_destruction_fails_closed_instead_of_aliasing_an_old_round");
    Destroy(one); Delete(two);
    for (std::size_t i=0;i<bindings.size();++i) bindings[i]={0x100000+i*0x200,b};
    bindings[0]={one,b}; bindings[1]={two,b};
    Create(args[0],args[1],args[2],args[3],args[4]);
    check(For(one)==invalid && For(two)==invalid,"a_full_binding_table_cannot_hide_reuse_of_an_existing_job_address");
    for (auto& entry:bindings) entry={};
    Put(stream.data(),0x68,std::uint32_t{kReplayRecordSize-1}); Create(args[0],args[1],args[2],args[3],args[4]);
    check(For(one)==invalid && !BeginNativeCommunityRoundScore(invalid,0,9001,comment,info),"invalid_capture_still_forwards_factory_but_cannot_submit");
    Destroy(one); Delete(two);
    check(!std::filesystem::exists(directory),"authored_disabled_fixture_creates_no_profile_or_network_storage");
    for (auto& entry:bindings) entry={}; current.reset(); test_seen.reset(); client.reset(); invalid.reset(); factory=nullptr; methods={}; score_table=0;
    backend_init=nullptr; backend_getter=nullptr; psb_root=nullptr; psb_has=nullptr; psb_item=nullptr; psb_integer=nullptr; copy_profile=nullptr; image_base=0;active_version=nullptr;
    std::printf("{\"event\":\"native_round_self_test\",\"passed\":%s,\"cases\":%u}\n",okay?"true":"false",cases); return okay;
}
