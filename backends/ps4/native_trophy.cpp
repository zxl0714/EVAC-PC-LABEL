#include "native_trophy.h"
#include "native_content.h"
#include "native_storage.h"
#include "native_memory.h"
#include "native_user.h"
#include "native_diagnostics.h"
#include <windows.h>
#include <objbase.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
constexpr int Error(unsigned code) { return static_cast<int>(0x80551600u + code); }
constexpr int kArgument=Error(4), kNotInitialized=Error(1), kInvalidHandle=Error(8), kInvalidContext=Error(9);
constexpr int kInvalidTrophy=Error(10), kAlreadyUnlocked=Error(12), kPlatinum=Error(13), kNotRegistered=Error(15);
constexpr int kAlreadyRegistered=Error(16), kBroken=Error(17), kContextExists=Error(19), kInvalidTrp=Error(22);
constexpr int kUnsupportedConf=Error(25), kAbort=Error(7), kInvalidUser=Error(37);
constexpr int kModuleNotLoaded=static_cast<int>(0x805a1001u), kModuleBusy=static_cast<int>(0x80020010u);
struct Failure { int code; };
void Require(bool condition, int code) { if (!condition) { throw Failure{code}; } }
template<class T> struct Com {
    T* value{};
    ~Com() { if (value) { value->Release(); } }
    T* operator->() const { return value; }
};
struct Definition { bool present{}; char grade{}; std::int16_t parent=-1; };
using Definitions=std::array<Definition,128>;
struct Context {
    int user{};
    unsigned label{};
    bool registered{};
    Definitions definitions{};
    std::array<std::uint64_t,128> unlocked{};
    std::array<std::byte,32> definition_hash{};
    std::unique_ptr<kt::save::Store> store;
    std::shared_ptr<kt::save::Mount> mount;
    std::uint64_t records{};
};
struct Handle { bool aborted{}; };
std::mutex gate;
unsigned references{};
int next_context=1, next_handle=1;
std::map<int,Context> contexts;
std::map<int,Handle> handles;

template<class Function> int Api(Function operation) noexcept {
    try { return operation(); }
    catch (const Failure& error) { return error.code; }
    catch (const std::bad_alloc&) { return Error(3); }
    catch (const kt::save::Failure& error) {
        std::fprintf(stderr,"[local trophy] %s\n",error.what());
        return error.error==kt::save::Error::NoSpace ? Error(18) : error.error==kt::save::Error::Corrupt ? kBroken : Error(0);
    }
    catch (const std::exception& error) { std::fprintf(stderr,"[local trophy] %s\n",error.what()); return Error(0); }
    catch (...) { return Error(0); }
}
Context& GetContext(int id) {
    Require(references!=0,kNotInitialized);
    const auto found=contexts.find(id);
    Require(found!=contexts.end(),kInvalidContext);
    return found->second;
}
Handle& GetHandle(int id) {
    Require(references!=0,kNotInitialized);
    const auto found=handles.find(id);
    Require(found!=handles.end(),kInvalidHandle);
    return found->second;
}
Context& Request(int context,int handle) {
    auto& current=GetContext(context);
    Require(!GetHandle(handle).aborted,kAbort);
    return current;
}
std::uint64_t Big(std::span<const std::byte> data) {
    std::uint64_t value{};
    for(const auto byte:data) { value=(value<<8)|std::to_integer<unsigned>(byte); }
    return value;
}
std::vector<std::byte> Read(kt::content::ReadOnlyFile& file,std::uint64_t offset,std::uint64_t bytes) {
    Require(bytes<=1024*1024 && offset<=file.Size() && bytes<=file.Size()-offset,kInvalidTrp);
    std::vector<std::byte> result(static_cast<std::size_t>(bytes));
    Require(file.ReadAt(offset,result)==result.size(),kInvalidTrp);
    return result;
}
int Attribute(IXmlReader* reader,const wchar_t* name,int fallback) {
    const auto found=reader->MoveToAttributeByName(name,nullptr);
    if(found==S_FALSE) { return fallback; }
    Require(found==S_OK,kUnsupportedConf);
    const wchar_t* value{}; UINT length{};
    Require(reader->GetValue(&value,&length)==S_OK && length && length<=4,kUnsupportedConf);
    unsigned index{}; bool negative=value[0]==L'-'; if(negative) { ++index; }
    Require(index<length,kUnsupportedConf);
    int number{};
    for(;index<length;++index) { Require(value[index]>=L'0' && value[index]<=L'9',kUnsupportedConf); number=number*10+value[index]-L'0'; }
    reader->MoveToElement();
    return negative ? -number : number;
}
Definitions ParseXml(std::span<const std::byte> bytes) {
    Com<IXmlReader> reader; Com<IStream> stream;
    Require(SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(&reader.value),nullptr)),kUnsupportedConf);
    Require(SUCCEEDED(reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit)),kUnsupportedConf);
    stream.value=SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size()));
    Require(stream.value && SUCCEEDED(reader->SetInput(stream.value)),kUnsupportedConf);
    Definitions result{}; bool root{}; unsigned count{},nodes{};
    XmlNodeType type{}; HRESULT status;
    while((status=reader->Read(&type))==S_OK) {
        UINT depth{}; Require(reader->GetDepth(&depth)==S_OK && depth<=16 && ++nodes<=10000,kUnsupportedConf);
        if(type!=XmlNodeType_Element) { continue; }
        const wchar_t* name{}; UINT length{};
        Require(reader->GetLocalName(&name,&length)==S_OK,kUnsupportedConf);
        const std::wstring_view local(name,length);
        if(depth==0) { Require(!root && local==L"trophyconf",kUnsupportedConf); root=true; }
        if(local!=L"trophy") { continue; }
        Require(root && depth==1,kUnsupportedConf);
        const auto id=Attribute(reader.value,L"id",-1),parent=Attribute(reader.value,L"pid",-1);
        Require(id>=0 && id<128 && parent>=-1 && parent<128 && !result[id].present,kUnsupportedConf);
        Require(reader->MoveToAttributeByName(L"ttype",nullptr)==S_OK,kUnsupportedConf);
        const wchar_t* grade{}; UINT grade_length{};
        Require(reader->GetValue(&grade,&grade_length)==S_OK && grade_length==1 &&
            (grade[0]==L'P' || grade[0]==L'G' || grade[0]==L'S' || grade[0]==L'B'),kUnsupportedConf);
        result[id]={true,static_cast<char>(grade[0]),static_cast<std::int16_t>(parent)};
        reader->MoveToElement(); ++count;
    }
    Require(status==S_FALSE && root && count,kUnsupportedConf);
    for(unsigned id=0;id<result.size();++id) {
        const auto& item=result[id];
        if(!item.present) { continue; }
        if(item.grade=='P') { Require(item.parent==-1,kUnsupportedConf); }
        if(item.parent>=0) { Require(item.parent!=static_cast<int>(id) && result[item.parent].present && result[item.parent].grade=='P',kUnsupportedConf); }
    }
    return result;
}
Definitions LoadDefinitions(unsigned label) {
    Require(label<100,Error(33));
    char name[64]; std::snprintf(name,sizeof(name),"/app0/sce_sys/trophy/trophy%02u.trp",label);
    auto file=OpenNativeContentFile(name);
    Require(file!=nullptr,Error(38));
    const auto header=Read(*file,0,96);
    Require(Big(std::span(header).subspan(0,4))==0xdca24d00 && Big(std::span(header).subspan(4,4))==3 &&
        Big(std::span(header).subspan(8,8))==file->Size(),kInvalidTrp);
    const auto count=Big(std::span(header).subspan(16,4)),size=Big(std::span(header).subspan(20,4));
    Require(count && count<=2048 && size==64,kInvalidTrp);
    const auto table=Read(*file,96,count*size);
    std::optional<std::vector<std::byte>> xml; bool encrypted{};
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    for(unsigned i=0;i<count;++i) {
        const auto entry=std::span(table).subspan(i*64,64);
        const auto* text=reinterpret_cast<const char*>(entry.data());
        const auto length=strnlen_s(text,32);
        Require(length && length<32,kInvalidTrp);
        const std::string_view entry_name(text,length);
        Require(entry_name.find_first_of("/\\:")==std::string_view::npos,kInvalidTrp);
        const auto offset=Big(entry.subspan(32,8)),bytes=Big(entry.subspan(40,8)),flags=Big(entry.subspan(48,4));
        Require(bytes && offset>=96+count*64 && offset<=file->Size() && bytes<=file->Size()-offset,kInvalidTrp);
        ranges.emplace_back(offset,offset+bytes);
        if(entry_name=="TROPCONF.XML" || entry_name=="TROPCONF.SFM" || entry_name=="TROPCONF.ESFM") {
            if(flags==3) { encrypted=true; }
            else { Require(!xml && flags<=1,kUnsupportedConf); xml=Read(*file,offset,bytes); }
        }
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i) { Require(ranges[i-1].second<=ranges[i].first,kInvalidTrp); }
    if(!xml && encrypted) {
        std::fputs("[local trophy] Encrypted trophy description is not supported; context remains unregistered\n",stderr);
        throw Failure{kUnsupportedConf};
    }
    Require(xml.has_value(),kUnsupportedConf);
    return ParseXml(*xml);
}
std::array<std::byte,32> DefinitionHash(const Definitions& definitions) {
    std::array<UCHAR,512> bytes{};
    for(unsigned i=0;i<128;++i) {
        bytes[i*4]=static_cast<UCHAR>(definitions[i].present); bytes[i*4+1]=static_cast<UCHAR>(definitions[i].grade);
        bytes[i*4+2]=static_cast<UCHAR>(definitions[i].parent); bytes[i*4+3]=static_cast<UCHAR>(definitions[i].parent>>8);
    }
    std::array<std::byte,32> result{};
    Require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),reinterpret_cast<PUCHAR>(result.data()),32)>=0,Error(0));
    return result;
}
std::uint32_t Checksum(std::span<const std::byte> data) {
    std::uint32_t value=0xffffffffu;
    for(const auto byte:data) { value^=std::to_integer<unsigned>(byte); for(unsigned bit=0;bit<8;++bit) { value=(value>>1)^(0xedb88320u & (0u-(value&1))); } }
    return ~value;
}
void AutoPlatinum(Context& context) {
    for(unsigned id=0;id<128;++id) {
        if(!context.definitions[id].present || context.definitions[id].grade!='P' || context.unlocked[id]) { continue; }
        bool any{},all=true; std::uint64_t latest{};
        for(unsigned child=0;child<128;++child) {
            if(!context.definitions[child].present || context.definitions[child].parent!=static_cast<int>(id)) { continue; }
            any=true; all&=context.unlocked[child]!=0; latest=std::max(latest,context.unlocked[child]);
        }
        if(any && all) { context.unlocked[id]=latest; }
    }
}
void LoadProgress(Context& context) {
    auto file=context.store->Open(context.mount,"unlocks.bin",{.read=true,.write=true,.create=true});
    const auto size=file->Info().size;
    Require(size<=128*64,kBroken);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    Require(file->ReadAt(0,bytes)==size,kBroken);
    const auto full=size/64;
    for(std::uint64_t i=0;i<full;++i) {
        const auto record=std::span(bytes).subspan(static_cast<std::size_t>(i*64),64);
        std::uint32_t id{},sequence{},checksum{},reserved{}; std::uint64_t time{};
        std::memcpy(&id,record.data()+40,4); std::memcpy(&reserved,record.data()+44,4); std::memcpy(&time,record.data()+48,8);
        std::memcpy(&sequence,record.data()+56,4); std::memcpy(&checksum,record.data()+60,4);
        Require(!std::memcmp(record.data(),"KTTRP001",8) && !std::memcmp(record.data()+8,context.definition_hash.data(),32) &&
            id<128 && context.definitions[id].present && !context.unlocked[id] && !reserved && time && sequence==i+1 && checksum==Checksum(record.first(60)),kBroken);
        context.unlocked[id]=time;
    }
    if(size%64) { std::fputs("[local trophy] Incomplete journal tail ignored; committed records retained\n",stderr); }
    context.records=full;
    AutoPlatinum(context);
}
void Persist(Context& context,std::span<const unsigned> ids) {
    Require(context.records+ids.size()<=128,kBroken);
    std::vector<std::byte> bytes(ids.size()*64);
    for(std::size_t i=0;i<ids.size();++i) {
        auto* record=bytes.data()+i*64; const auto id=ids[i];
        std::memcpy(record,"KTTRP001",8); std::memcpy(record+8,context.definition_hash.data(),32);
        std::memcpy(record+40,&id,4); std::memcpy(record+48,&context.unlocked[id],8);
        const auto sequence=static_cast<std::uint32_t>(context.records+i+1);
        std::memcpy(record+56,&sequence,4); const auto checksum=Checksum({record,60}); std::memcpy(record+60,&checksum,4);
    }
    auto file=context.store->Open(context.mount,"unlocks.bin",{.read=true,.write=true});
    std::uint64_t end{};
    Require(file->WriteAt(context.records*64,bytes,end)==bytes.size(),Error(0));
    file->Flush(); context.records+=ids.size();
}
int KT_SYSV CreateContext(int* output,int user,unsigned label,std::uint64_t options) {
    return Api([&] {
        Require(output && !options,kArgument);
        std::lock_guard lock(gate); Require(references!=0,kNotInitialized);
        Require(user==0x1000,kInvalidUser); Require(NativeUserInitialized(),Error(29)); Require(label<100,Error(33));
        Require(std::none_of(contexts.begin(),contexts.end(),[&](const auto& item){return item.second.user==user && item.second.label==label;}),kContextExists);
        Require(contexts.size()<8 && next_context<INT_MAX,Error(35));
        const int id=next_context++;
        auto [it,inserted]=contexts.try_emplace(id); (void)inserted; it->second.user=user; it->second.label=label;
        if(!CopyToNativeMemory(output,&id,sizeof(id))) { contexts.erase(it); return kArgument; }
        std::printf("{\"event\":\"native_trophy_context\",\"context\":%d,\"user\":%d,\"service_label\":%u,\"registered\":false}\n",id,user,label);
        return 0;
    });
}
int KT_SYSV CreateHandle(int* output) {
    return Api([&] {
        Require(output!=nullptr,kArgument); std::lock_guard lock(gate); Require(references!=0,kNotInitialized);
        Require(handles.size()<4 && next_handle<INT_MAX,Error(36)); const int id=next_handle++;
        handles.emplace(id,Handle{});
        if(!CopyToNativeMemory(output,&id,sizeof(id))) { handles.erase(id); return kArgument; }
        return 0;
    });
}
int KT_SYSV DestroyContext(int context) {
    return Api([&] { std::lock_guard lock(gate); auto& current=GetContext(context);
        if(current.registered) { current.store->Unmount(current.mount); current.mount.reset(); }
        contexts.erase(context); return 0; });
}
int KT_SYSV DestroyHandle(int handle) {
    return Api([&] { std::lock_guard lock(gate); GetHandle(handle); handles.erase(handle); return 0; });
}
int KT_SYSV AbortHandle(int handle) {
    return Api([&] { std::lock_guard lock(gate); GetHandle(handle).aborted=true; return 0; });
}
int KT_SYSV RegisterContext(int context,int handle,std::uint64_t options) {
    return Api([&] {
        Require(!options,kArgument); std::lock_guard lock(gate); auto& current=Request(context,handle);
        Require(!current.registered,kAlreadyRegistered);
        auto definitions=LoadDefinitions(current.label);
        const auto root=NativeStorageRoot(); const auto title=NativeContentTitleId();
        Require(!root.empty() && title.size()==9,Error(32));
        Context candidate; candidate.user=current.user; candidate.label=current.label; candidate.definitions=definitions;
        candidate.definition_hash=DefinitionHash(definitions);
        const auto trophy_root=root/"profiles"/"1000"/title/"trophy";
        kt::save::Store::CreateRoot(trophy_root);
        candidate.store=std::make_unique<kt::save::Store>(trophy_root);
        bool created{};
        candidate.mount=candidate.store->MountSlot("service-"+std::to_string(current.label),true,true,false,96,created);
        LoadProgress(candidate); candidate.registered=true;
        current=std::move(candidate);
        std::printf("{\"event\":\"native_trophy_registered\",\"context\":%d,\"records\":%llu,\"online\":false}\n",context,current.records);
        return 0;
    });
}
int KT_SYSV Unlock(int context,int handle,int trophy,int* platinum) {
    return Api([&] {
        Require(platinum!=nullptr,kArgument);
        auto output=BeginNativeHostWrite(platinum,sizeof(*platinum));
        Require(static_cast<bool>(output),kArgument);
        std::lock_guard lock(gate); auto& current=Request(context,handle); Require(current.registered,kNotRegistered);
        Require(trophy>=0 && trophy<128 && current.definitions[trophy].present,kInvalidTrophy);
        Require(current.definitions[trophy].grade!='P',kPlatinum); Require(!current.unlocked[trophy],kAlreadyUnlocked);
        const auto previous=current.unlocked;
        const auto time=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        current.unlocked[trophy]=static_cast<std::uint64_t>(std::max<std::int64_t>(1,time)); AutoPlatinum(current);
        std::array<unsigned,128> changed{}; unsigned count=1; changed[0]=static_cast<unsigned>(trophy); int earned=-1;
        for(unsigned id=0;id<128;++id) { if(id!=static_cast<unsigned>(trophy) && current.unlocked[id] && !previous[id]) { changed[count++]=id; if(current.definitions[id].grade=='P') { earned=static_cast<int>(id); } } }
        try { Persist(current,std::span(changed).first(count)); } catch(...) { current.unlocked=previous; throw; }
        std::memcpy(platinum,&earned,sizeof(earned));
        std::printf("{\"event\":\"native_trophy_unlock\",\"context\":%d,\"trophy\":%d,\"platinum\":%d,\"durable\":true}\n",context,trophy,earned);
        return 0;
    });
}
int KT_SYSV UnlockState(int context,int handle,std::array<std::uint32_t,4>* flags,unsigned* count) {
    return Api([&] {
        Require(flags && count,kArgument);
        auto flags_output=BeginNativeHostWrite(flags,sizeof(*flags));
        Require(static_cast<bool>(flags_output),kArgument);
        auto count_output=BeginNativeHostWrite(count,sizeof(*count));
        Require(static_cast<bool>(count_output),kArgument);
        std::lock_guard lock(gate); const auto& current=Request(context,handle); Require(current.registered,kNotRegistered);
        std::array<std::uint32_t,4> value{}; unsigned number{};
        for(unsigned id=0;id<128;++id) { if(current.definitions[id].present) { ++number; } if(current.unlocked[id]) { value[id/32]|=1u<<(id%32); } }
        std::memcpy(flags,value.data(),sizeof(value));
        std::memcpy(count,&number,sizeof(number));
        return 0;
    });
}
}
int TrophyModule(TrophyModuleAction action) {
    return Api([&] {
        std::lock_guard lock(gate);
        if(action==TrophyModuleAction::IsLoaded) { return references ? 0 : kModuleNotLoaded; }
        if(action==TrophyModuleAction::Load) { Require(references<UINT_MAX,Error(3)); ++references; }
        else { if(!references) { return kModuleNotLoaded; } if(references==1 && (!contexts.empty() || !handles.empty())) { return kModuleBusy; } --references; }
        std::printf("{\"event\":\"native_trophy_module\",\"operation\":\"%s\",\"references\":%u,\"online\":false}\n",action==TrophyModuleAction::Load ? "load" : "unload",references);
        return 0;
    });
}
std::uint64_t ResolveTrophyImport(std::string_view identifier,std::string_view library,std::string_view module) {
    if(library!="libSceNpTrophy" || module!=library) { return 0; }
    const auto id=identifier.substr(0,identifier.find('#'));
    if(id=="XbkjbobZlCY") { return reinterpret_cast<std::uint64_t>(&CreateContext); }
    if(id=="q7U6tEAQf7c") { return reinterpret_cast<std::uint64_t>(&CreateHandle); }
    if(id=="E1Wrwd07Lr8") { return reinterpret_cast<std::uint64_t>(&DestroyContext); }
    if(id=="GNcF4oidY0Y") { return reinterpret_cast<std::uint64_t>(&DestroyHandle); }
    if(id=="aTnHs7W-9Uk") { return reinterpret_cast<std::uint64_t>(&AbortHandle); }
    if(id=="TJCAxto9SEU") { return reinterpret_cast<std::uint64_t>(&RegisterContext); }
    if(id=="28xmRUFao68") { return reinterpret_cast<std::uint64_t>(&Unlock); }
    if(id=="LHuSmO3SLd8") { return reinterpret_cast<std::uint64_t>(&UnlockState); }
    return 0;
}
bool TrophySelfTest(const std::filesystem::path& app,const std::filesystem::path& user,bool read_back) {
    bool passed=true;
    const auto check=[&](const char* name,bool value) {
        passed&=value;
        std::printf("{\"event\":\"trophy_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,value?"true":"false");
    };
    ConfigureNativeContent(app,{}, {},user); ConfigureNativeStorage(user);
    using UserInit=int(KT_SYSV*)(const void*);
    const auto initialize=reinterpret_cast<UserInit>(ResolveUserImport("j3YMu1MVNNo","libSceUserService","libSceUserService"));
    struct { int priority; std::byte reserved[252]; } init{0x100,{}};
    check("user_initialized",initialize && initialize(&init)==0);
    int sentinel=0x73737373;
    check("unloaded_does_not_publish_handle",CreateHandle(&sentinel)==kNotInitialized && sentinel==0x73737373);
    check("wrong_namespace_rejected",!ResolveTrophyImport("q7U6tEAQf7c","libSceNpManager","libSceNpTrophy") &&
        !ResolveTrophyImport("q7U6tEAQf7c","libSceNpTrophy","libSceNpManager"));
    check("module_reference_count",TrophyModule(TrophyModuleAction::Load)==0 && TrophyModule(TrophyModuleAction::Load)==0 &&
        TrophyModule(TrophyModuleAction::Unload)==0 && TrophyModule(TrophyModuleAction::IsLoaded)==0);
    int context=-1,handle=-1;
    check("invalid_context_parameters_preserve_output",CreateContext(&sentinel,0x1000,0,1)==kArgument &&
        CreateContext(&sentinel,-1,0,0)==kInvalidUser && sentinel==0x73737373);
    check("context_has_real_identity",CreateContext(&context,0x1000,0,0)==0 && context>0);
    check("duplicate_context_rejected",CreateContext(&sentinel,0x1000,0,0)==kContextExists && sentinel==0x73737373);
    check("invalid_output_does_not_consume_live_context",CreateContext(reinterpret_cast<int*>(1),0x1000,1,0)==kArgument);
    std::array<int,7> extra{}; bool bounded=true;
    for(unsigned i=0;i<extra.size();++i) { bounded&=CreateContext(&extra[i],0x1000,i+1,0)==0; }
    bounded&=CreateContext(&sentinel,0x1000,8,0)==Error(35) && sentinel==0x73737373;
    for(const auto id:extra) { bounded&=DestroyContext(id)==0; }
    check("contexts_bounded_and_retired",bounded);
    check("handle_has_real_identity",CreateHandle(&handle)==0 && handle>0);
    std::array<int,3> extras{}; bounded=true;
    for(auto& id:extras) { bounded&=CreateHandle(&id)==0; }
    bounded&=CreateHandle(&sentinel)==Error(36) && sentinel==0x73737373;
    for(const auto id:extras) { bounded&=DestroyHandle(id)==0; }
    check("handles_bounded_and_retired",bounded);
    check("live_objects_pin_module",TrophyModule(TrophyModuleAction::Unload)==kModuleBusy);
    std::array<std::uint32_t,4> flags{}; unsigned count{}; int platinum=0x73737373;
    check("unregistered_access_is_failure",Unlock(context,handle,1,&platinum)==kNotRegistered && platinum==0x73737373);
    check("register_validates_identity_and_options",RegisterContext(-1,handle,0)==kInvalidContext &&
        RegisterContext(context,-1,0)==kInvalidHandle && RegisterContext(context,handle,1)==kArgument);
    const int registration=RegisterContext(context,handle,0);
    std::printf("{\"event\":\"trophy_register_test_result\",\"result\":%d}\n",registration);
    check("registered_only_after_definition_and_progress_load",registration==0);
    if(registration!=0) { return false; }
    check("duplicate_registration_rejected",RegisterContext(context,handle,0)==kAlreadyRegistered);
    flags.fill(0x73737373); count=0x73737373;
    check("invalid_state_output_preserves_other_outputs",UnlockState(context,handle,&flags,reinterpret_cast<unsigned*>(1))==kArgument &&
        flags==std::array<std::uint32_t,4>{0x73737373,0x73737373,0x73737373,0x73737373} && count==0x73737373 &&
        UnlockState(context,handle,reinterpret_cast<std::array<std::uint32_t,4>*>(1),&count)==kArgument && count==0x73737373);
    if(!read_back) {
        check("invalid_unlock_output_does_not_publish_progress",Unlock(context,handle,1,reinterpret_cast<int*>(1))==kArgument);
        check("new_progress_empty_with_total_trophy_count",UnlockState(context,handle,&flags,&count)==0 && count==3 && flags==std::array<std::uint32_t,4>{});
        check("invalid_trophies_do_not_write_output",Unlock(context,handle,-1,&platinum)==kInvalidTrophy &&
            Unlock(context,handle,127,&platinum)==kInvalidTrophy && Unlock(context,handle,0,&platinum)==kPlatinum && platinum==0x73737373);
        check("first_unlock_is_durable",Unlock(context,handle,1,&platinum)==0 && platinum==-1);
        check("duplicate_unlock_rejected",Unlock(context,handle,1,&platinum)==kAlreadyUnlocked);
        check("second_unlock_completes_platinum",Unlock(context,handle,2,&platinum)==0 && platinum==0);
    }
    check("progress_contains_child_and_platinum_flags",UnlockState(context,handle,&flags,&count)==0 && count==3 && flags[0]==7 && !flags[1] && !flags[2] && !flags[3]);
    check("aborted_handle_cannot_issue_requests",AbortHandle(handle)==0 && UnlockState(context,handle,&flags,&count)==kAbort);
    const int stale_handle=handle;
    check("destroyed_handle_cannot_be_reused",DestroyHandle(handle)==0 && AbortHandle(stale_handle)==kInvalidHandle &&
        CreateHandle(&handle)==0 && handle!=stale_handle);
    const int stale_context=context;
    check("destroyed_context_cannot_be_reused",DestroyContext(context)==0 && DestroyContext(stale_context)==kInvalidContext &&
        CreateContext(&context,0x1000,0,0)==0 && context!=stale_context && RegisterContext(context,handle,0)==0);
    check("reopen_reads_durable_progress",UnlockState(context,handle,&flags,&count)==0 && count==3 && flags[0]==7);
    check("clean_teardown_unloads",DestroyContext(context)==0 && DestroyHandle(handle)==0 && TrophyModule(TrophyModuleAction::Unload)==0 &&
        TrophyModule(TrophyModuleAction::IsLoaded)==kModuleNotLoaded);
    std::printf("{\"event\":\"trophy_self_test\",\"read_back\":%s,\"passed\":%s}\n",read_back?"true":"false",passed?"true":"false");
    return passed;
}
