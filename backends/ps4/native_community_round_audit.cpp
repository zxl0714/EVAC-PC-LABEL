#include "native_community_round_audit.h"
#include "community/protocol.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
using kt::community::Json;
constexpr std::size_t kMethod = 0x1e4ca0, kSlot = 0x2d8ed8, kRecord = 0x80000;
constexpr char kSource[] = "a32b4b798cc09d8b3f756acde33d679dbb7fec1a54eec50cc179f3f3694ac073";
constexpr unsigned char kPrefix[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
using Method = std::uint64_t(KT_SYSV*)(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
bool enabled{};
Method original{};
std::atomic<unsigned> observations{};
bool Admit() {
    auto count = observations.load();
    while (count < 8) { if (observations.compare_exchange_weak(count,count+1)) return true; }
    return false;
}

void Require(bool value) { if (!value) throw std::runtime_error("Invalid bounded round audit input"); }
std::uint64_t Add(std::uint64_t address, std::size_t bytes) {
    Require(address && bytes <= (std::numeric_limits<std::uint64_t>::max)() - address);
    return address + bytes;
}
void Read(std::uint64_t source, void* output, std::size_t bytes) {
    (void)Add(source, bytes);
    SIZE_T received{};
    Require(ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(source), output, bytes, &received) && received == bytes);
}
template<class T> T Value(std::uint64_t address, std::size_t offset=0) {
    T value{}; Read(Add(address, offset), &value, sizeof(value)); return value;
}
std::string Text(std::uint64_t object) {
    const auto length = Value<std::uint64_t>(object, 0x18), capacity = Value<std::uint64_t>(object, 0x20);
    Require(length && length <= 32 && length <= capacity && (capacity >= 16 || capacity == 15));
    const auto data = capacity < 16 ? Add(object, 8) : Value<std::uint64_t>(object, 8);
    std::array<char, 33> text{}; Read(data, text.data(), static_cast<std::size_t>(length + 1));
    Require(!text[length] && std::all_of(text.begin(), text.begin()+length, [](unsigned char c) { return c >= 0x20 && c <= 0x7e; }));
    return {text.data(), static_cast<std::size_t>(length)};
}
std::size_t Count(std::uint64_t begin, std::uint64_t end, std::size_t stride, std::size_t limit) {
    Require(end >= begin && (end - begin) % stride == 0 && (end - begin) / stride <= limit);
    Require(begin || begin == end);
    return static_cast<std::size_t>((end - begin) / stride);
}
Json Snapshot(std::uint64_t boards, std::uint64_t metadata, std::uint64_t stream) {
    const auto begin = Value<std::uint64_t>(boards, 8), end = Value<std::uint64_t>(boards, 16);
    const auto count = Count(begin, end, 40, 22); Require(count != 0);
    Json board_ids = Json::array();
    for (std::size_t i=0; i<count; ++i) {
        const auto name = Text(Add(begin, i*40));
        Require(name.size() <= 2 && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }));
        board_ids.push_back(name);
    }
    const auto score = Value<std::int64_t>(metadata, 0x38); Require(score >= 0);
    const auto column_begin = Value<std::uint64_t>(metadata, 0x80), column_end = Value<std::uint64_t>(metadata, 0x88);
    const auto columns = Count(column_begin, column_end, 0x130, 16);
    Json fields = Json::array();
    for (std::size_t i=0; i<columns; ++i) {
        const auto column = Add(column_begin, i*0x130);
        const auto type = Value<std::uint32_t>(column); Require(type == 0 || type == 3);
        const auto key = Text(Add(column, 8));
        Require(key.size() <= 8 && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; }));
        fields.push_back(Json{{"key",key},{"type",type},{"value",Value<std::int32_t>(column,0x30)}});
    }
    Require(Value<std::uint32_t>(stream, 0x68) == kRecord);
    std::vector<std::byte> record(kRecord);
    Read(Value<std::uint64_t>(stream, 0x60), record.data(), record.size());
    const auto word = [&](std::size_t offset) { std::uint32_t v{}; std::memcpy(&v, record.data()+offset, sizeof(v)); return v; };
    Require(word(0) == 0x10001 && word(4) == 0x10001 && word(108) == static_cast<std::uint64_t>(score));
    // Header-only observation: no inflate, state interpretation or player call.
    return Json{{"event","native_community_round_audit"},{"valid",true},{"observational_only",true},
                {"boards",std::move(board_ids)},{"score",score},{"columns",std::move(fields)},
                {"record_bytes",record.size()},{"record_sha256",kt::community::Sha256(record)},
                {"total_frames",word(16)},{"consumed_frames",word(20)},{"envelope_validated",false}};
}
std::uint64_t KT_SYSV Observe(std::uint64_t backend, std::uint64_t boards, std::uint64_t metadata,
                              std::uint64_t stream, std::uint64_t result) {
    const auto saved_error = GetLastError(); const auto saved_errno = errno;
    if (Admit()) {
        try { std::puts(Snapshot(boards, metadata, stream).dump().c_str()); }
        catch (...) { std::puts("{\"event\":\"native_community_round_audit\",\"valid\":false,\"observational_only\":true}"); }
    }
    errno = saved_errno; SetLastError(saved_error);
    return original(backend, boards, metadata, stream, result);
}
}
bool ConfigureNativeCommunityRoundAudit() {
    if (enabled) return false;
    enabled = true; return true;
}
void InstallNativeCommunityRoundAudit(unsigned char* image, std::size_t bytes,
                                     std::span<const std::uint8_t,32> source) {
    if (!enabled) return;
    Require(!original && image && bytes >= kSlot + 8 && bytes >= kMethod + sizeof(kPrefix));
    std::string digest;
    for (auto b: source) { digest += "0123456789abcdef"[b >> 4]; digest += "0123456789abcdef"[b & 15]; }
    Require(digest == kSource && std::memcmp(image+kMethod,kPrefix,sizeof(kPrefix)) == 0);
    std::uint64_t target{}; std::memcpy(&target,image+kSlot,8);
    Require(target == reinterpret_cast<std::uint64_t>(image+kMethod));
    original = reinterpret_cast<Method>(target);
    target = reinterpret_cast<std::uint64_t>(&Observe);
    // The loader calls this once, after relocation and before segment protection.
    std::memcpy(image+kSlot,&target,8);
    std::puts("{\"event\":\"native_community_round_audit_installed\",\"observational_only\":true}");
}

namespace {
template<class T> void Put(void* destination, std::size_t offset, T value) { std::memcpy(static_cast<std::byte*>(destination)+offset,&value,sizeof(value)); }
void SmallString(void* output, std::size_t offset, const char* text) {
    const auto size = std::strlen(text); Require(size && size < 16);
    std::memcpy(static_cast<std::byte*>(output)+offset+8,text,size+1);
    Put(output,offset+24,static_cast<std::uint64_t>(size)); Put(output,offset+32,std::uint64_t{15});
}
std::array<std::uint64_t,5> forwarded{};
std::uint64_t KT_SYSV Forward(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d,std::uint64_t e) {
    forwarded={a,b,c,d,e}; return 0x123456789abcdef0ull;
}
}
bool CommunityRoundAuditSelfTest() {
    Require(!enabled && !original);
    unsigned cases{}; bool okay=true;
    const auto check=[&](bool value,const char* name) {
        ++cases; okay &= value;
        std::printf("{\"event\":\"community_round_audit_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n",name,value?"true":"false");
    };
    std::array<std::byte,80> strings{}; SmallString(strings.data(),0,"0"); SmallString(strings.data(),40,"1");
    std::array<std::uint64_t,3> boards{0,reinterpret_cast<std::uint64_t>(strings.data()),reinterpret_cast<std::uint64_t>(strings.data()+strings.size())};
    std::array<std::byte,0x130> column{}; SmallString(column.data(),8,"2"); Put(column.data(),0x30,std::int32_t{7});
    std::array<std::byte,0x98> meta{};
    Put(meta.data(),0x38,std::int64_t{3850}); Put(meta.data(),0x80,reinterpret_cast<std::uint64_t>(column.data()));
    Put(meta.data(),0x88,reinterpret_cast<std::uint64_t>(column.data()+column.size()));
    std::vector<std::byte> raw(kRecord); Put(raw.data(),0,std::uint32_t{0x10001}); Put(raw.data(),4,std::uint32_t{0x10001}); Put(raw.data(),108,std::uint32_t{3850});
    std::array<std::byte,0x70> stream{}; Put(stream.data(),0x60,reinterpret_cast<std::uint64_t>(raw.data())); Put(stream.data(),0x68,std::uint32_t{kRecord});
    const auto snapshot=[&] { return Snapshot(reinterpret_cast<std::uint64_t>(boards.data()),reinterpret_cast<std::uint64_t>(meta.data()),reinterpret_cast<std::uint64_t>(stream.data())); };
    const auto rejects=[&](auto operation) { try { operation(); return false; } catch (...) { return true; } };
    auto result=snapshot();
    check(result["boards"] == Json::array({"0","1"}) && result["score"]==3850 && result["columns"][0]["value"]==7 &&
          result["record_sha256"]==kt::community::Sha256(raw) && !result["envelope_validated"].get<bool>(),"owned_header_only_snapshot_with_bounded_boards_and_columns");
    const std::array<char,2> heap_name{'0',0};
    Put(strings.data(),8,reinterpret_cast<std::uint64_t>(heap_name.data())); Put(strings.data(),32,std::uint64_t{31});
    check(snapshot()["boards"][0]=="0","bounded_heap_backed_string_read"); SmallString(strings.data(),0,"0");
    raw[300]=std::byte{1}; check(result["record_sha256"]!=kt::community::Sha256(raw),"snapshot_keeps_no_guest_pointer_or_mutable_body");
    boards[2]++; check(rejects(snapshot),"misaligned_vector_rejected"); boards[2]--;
    const auto end=boards[2]; boards[2]=boards[1]+23*40; check(rejects(snapshot),"oversized_board_count_rejected_before_following_entries"); boards[2]=end;
    Put(stream.data(),0x68,std::uint32_t{kRecord-1}); check(rejects(snapshot),"nonfixed_record_size_rejected"); Put(stream.data(),0x68,std::uint32_t{kRecord});
    Put(meta.data(),0x38,std::int64_t{3851}); check(rejects(snapshot),"metadata_score_mismatch_rejected"); Put(meta.data(),0x38,std::int64_t{3850});
    Put(column.data(),0,std::uint32_t{3}); check(snapshot()["columns"][0]["type"]==3,"boolean_column_type_preserved"); Put(column.data(),0,std::uint32_t{0});
    Put(column.data(),0,std::uint32_t{2}); check(rejects(snapshot),"uninterpreted_column_type_rejected"); Put(column.data(),0,std::uint32_t{0});
    check(rejects([&] { Snapshot(1,reinterpret_cast<std::uint64_t>(meta.data()),reinterpret_cast<std::uint64_t>(stream.data())); }),"unreadable_pointer_rejected_without_faulting");
    check(rejects([&] { Snapshot((std::numeric_limits<std::uint64_t>::max)()-1,reinterpret_cast<std::uint64_t>(meta.data()),reinterpret_cast<std::uint64_t>(stream.data())); }),"pointer_addition_overflow_rejected_before_read");
    std::array<std::uint8_t,32> source{};
    const auto nibble=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
    for (std::size_t i=0;i<source.size();++i) source[i]=static_cast<std::uint8_t>(nibble(kSource[2*i])*16+nibble(kSource[2*i+1]));
    std::vector<unsigned char> image(kSlot+8); std::memcpy(image.data()+kMethod,kPrefix,sizeof(kPrefix));
    auto target=reinterpret_cast<std::uint64_t>(image.data()+kMethod); std::memcpy(image.data()+kSlot,&target,8);
    InstallNativeCommunityRoundAudit(nullptr,0,source);
    check(!original && !enabled,"ordinary_disabled_load_does_not_inspect_or_patch_an_image");
    check(ConfigureNativeCommunityRoundAudit() && !ConfigureNativeCommunityRoundAudit(),"audit_is_explicit_and_configured_once");
    source[0]^=1;
    check(rejects([&] { InstallNativeCommunityRoundAudit(image.data(),image.size(),source); }) && !original,"wrong_source_identity_rejected_before_modifying_dispatch"); source[0]^=1;
    image[kMethod]^=1;
    check(rejects([&] { InstallNativeCommunityRoundAudit(image.data(),image.size(),source); }) && !original,"changed_entry_bytes_rejected"); image[kMethod]^=1;
    Put(image.data(),kSlot,std::uint64_t{73});
    check(rejects([&] { InstallNativeCommunityRoundAudit(image.data(),image.size(),source); }) && !original,"unexpected_relocated_vtable_target_rejected"); Put(image.data(),kSlot,target);
    InstallNativeCommunityRoundAudit(image.data(),image.size(),source);
    check(original==reinterpret_cast<Method>(target) && Value<std::uint64_t>(reinterpret_cast<std::uint64_t>(image.data()),kSlot)==reinterpret_cast<std::uint64_t>(&Observe),"known_relocation_redirects_to_audit_and_keeps_original_target");
    original=&Forward;
    SetLastError(97); errno=EDOM;
    const std::array<std::uint64_t,5> args{71,reinterpret_cast<std::uint64_t>(boards.data()),reinterpret_cast<std::uint64_t>(meta.data()),reinterpret_cast<std::uint64_t>(stream.data()),83};
    const auto returned=Observe(args[0],args[1],args[2],args[3],args[4]);
    check(returned==0x123456789abcdef0ull && forwarded==args && GetLastError()==97 && errno==EDOM,"sysv_five_argument_forwarding_and_error_state_preserved");
    observations=0; unsigned admitted{}; for (unsigned i=0;i<16;++i) admitted+=Admit();
    check(admitted==8 && !Admit(),"observations_are_bounded_without_counter_wraparound");
    original=nullptr; observations=0; enabled=false;
    std::printf("{\"event\":\"community_round_audit_self_test\",\"passed\":%s,\"cases\":%u}\n",okay?"true":"false",cases);
    return okay;
}
