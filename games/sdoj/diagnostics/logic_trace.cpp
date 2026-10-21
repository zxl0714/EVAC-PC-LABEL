#include "logic_trace.h"
#include "input_trace_policy.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

REXCVAR_DEFINE_BOOL(sdoj_logic_trace,false,"Diagnostics",
    "Read-only bounded per-logic-frame input/command SHA-256/slowdown audit")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(sdoj_logic_content_from,0,"Diagnostics",
    "First logical frame for exact command bytes; zero disables; requires logic trace")
    .range(0,32768).lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(sdoj_logic_content_count,0,"Diagnostics",
    "Number of exact completed command snapshots, at most 16 per module generation")
    .range(0,16).lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr std::uint64_t Limit=32768;
struct State {
    std::uint64_t generation{},logic{},batch{},emitted{};
    std::uint32_t thread{},buffer{},command_begins{};
    sdoj::diagnostics::CoreInputSample input{};
    bool pending{},input_valid{};
    struct Binding { std::uint32_t buffer{}; std::uint64_t logic{}; };
    std::array<Binding,4> completed{};
    std::uint64_t read_logic{},metrics_emitted{},slow_metrics_sequence{};
    std::uint32_t tilemap_emitted{};
    std::uint32_t slow_stat0{},slow_stat1{};
    bool slow_valid{},read_seen{};
};
std::array<State,2> states;
std::mutex gate;
std::optional<std::span<const std::uint8_t>> Read(std::uint32_t address,std::uint32_t size) {
    if(!address || !size || std::uint64_t(address)+size>0x100000000ull)return {};
    auto* runtime=rex::Runtime::instance();
    auto* memory=runtime ? runtime->memory() : nullptr;
    auto* heap=memory ? memory->LookupHeap(address) : nullptr;
    if(!heap || heap!=memory->LookupHeap(address+size-1) ||
       !(std::uint32_t(heap->QueryRangeAccess(address,address+size-1))&1))return {};
    return std::span<const std::uint8_t>(memory->TranslateVirtual<std::uint8_t*>(address),size);
}
std::optional<std::uint32_t> Word(std::uint32_t address) {
    const auto bytes=Read(address,4);
    if(!bytes)return {};
    return (std::uint32_t((*bytes)[0])<<24)|(std::uint32_t((*bytes)[1])<<16)|
           (std::uint32_t((*bytes)[2])<<8)|(*bytes)[3];
}
struct CoreState { std::uint32_t outer,play,end; };
constexpr CoreState CoreStateAddresses(unsigned module) {
    // Disc CA022100: 88080124, 8807F030, 88080694.
    // Disc CA022110: 88080F14, 8807FE08, 8808146C.
    // These are three independently reviewed addresses, not a common offset.
    return module==1 ? CoreState{0x886ACB48,0x886ACC30,0x886B4798}
                     : CoreState{0x886B6600,0x886B66E8,0x886BCCC0};
}
bool SelectedContent(std::uint64_t logic) {
    const auto first=REXCVAR_GET(sdoj_logic_content_from);
    const auto count=std::min(REXCVAR_GET(sdoj_logic_content_count),16u);
    return first && logic>=first && logic-first<count;
}
void Content(unsigned module,const State& state,std::span<const std::uint8_t> bytes) {
    if(!SelectedContent(state.logic))return;
    // Emit the complete immutable batch or an explicit rejection, never a
    // truncated prefix that could hide the first differing command.
    if(bytes.size()>65536) {
        REXLOG_WARN("SDOJ_LOGIC_CONTENT module={} generation={} logic={} bytes={} issue=extent_limit",
                    module,state.generation,state.logic,bytes.size());
        return;
    }
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t offset=0;offset<bytes.size();offset+=256) {
        std::string chunk;
        for(auto i=offset;i<std::min(offset+256,bytes.size());++i) {
            chunk+=hex[bytes[i]>>4];chunk+=hex[bytes[i]&15];
        }
        REXLOG_INFO("SDOJ_LOGIC_CONTENT module={} generation={} logic={} bytes={} offset={} hex={} no_guest_writes",
                    module,state.generation,state.logic,bytes.size(),offset,chunk);
    }
}
}

extern "C" __declspec(dllexport) bool SdojLogicTraceEnabled() { return REXCVAR_GET(sdoj_logic_trace); }
extern "C" __declspec(dllexport) bool SdojLogicContentEnabled() {
    return REXCVAR_GET(sdoj_logic_trace) && REXCVAR_GET(sdoj_logic_content_from) &&
           REXCVAR_GET(sdoj_logic_content_count);
}

// Disc CA022100 88032D68: the tilemap emitter has already computed its
// 64x64 source-map origin and packed destination. Observe the 11x9 cells it
// will actually index, including the first column, before emitting commands.
extern "C" __declspec(dllexport) void SdojLogicTilemapEvent(
    std::uint32_t map,std::uint32_t row,std::uint32_t column,std::uint32_t xy,
    std::uint32_t bank,std::uint32_t color,std::uint32_t observed_lr) {
    if(!SdojLogicContentEnabled())return;
    std::uint64_t generation{},logic{};
    std::uint32_t sample{};
    {
        std::lock_guard lock(gate);
        auto& state=states[0];
        if(!state.pending || state.thread!=GetCurrentThreadId() ||
           !SelectedContent(state.logic) || state.tilemap_emitted>=32)return;
        generation=state.generation;logic=state.logic;sample=++state.tilemap_emitted;
    }
    const auto bytes=Read(map,64*64*8);
    if(!bytes || row>=64 || column>=64) {
        REXLOG_WARN("SDOJ_TILEMAP module=1 generation={} logic={} sample={} issue=map_or_origin",
                    generation,logic,sample);
        return;
    }
    // The native emitter reads the first word of each eight-byte map entry.
    // Log complete rows, not inferred sprites or a silently truncated map.
    for(unsigned y=0;y<9;++y) {
        std::string cells;
        for(unsigned x=0;x<11;++x) {
            const auto offset=(((row+y)&63)*64+((column+x)&63))*8;
            const auto* p=bytes->data()+offset;
            const auto word=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
                            (std::uint32_t(p[2])<<8)|p[3];
            cells+=fmt::format("{:08X}",word);
        }
        REXLOG_INFO("SDOJ_TILEMAP module=1 generation={} logic={} sample={} map={:08X} source_row={} source_column={} xy={:08X} bank={:08X} color={:08X} observed_lr={:08X} row_offset={} cells={} no_guest_writes",
            generation,logic,sample,map,row,column,xy,bank,color,observed_lr,y,cells);
    }
}

void SdojLogicTraceEvent(std::uint32_t module,std::uint32_t phase,std::uint32_t argument) {
    if(!REXCVAR_GET(sdoj_logic_trace) || module<1 || module>2 || (phase!=0 && phase!=1 && phase!=5))return;
    std::lock_guard lock(gate);
    auto& state=states[module-1];
    if(phase==0) {
        const auto generation=state.generation+1; state={}; state.generation=generation;
        return;
    }
    if(phase==1) {
        // This is the pre-copy live port snapshot. Native replay playback
        // supplies its gameplay inputs later; do not label this as the decoded
        // replay input. Completed command bytes remain a playback comparison.
        ++state.logic; state.pending=true; state.buffer=0; state.command_begins=0;
        state.thread=GetCurrentThreadId(); state.input_valid=false;
        state.slow_valid=false;
        state.tilemap_emitted=0;
        if(const auto bytes=Read(sdoj::diagnostics::CoreInputBlock(module),176))
            if(const auto input=sdoj::diagnostics::DecodeCoreInput(*bytes)) {
                state.input=*input; state.input_valid=true;
            }
    } else if(state.pending && state.thread==GetCurrentThreadId()) {
        ++state.batch; ++state.command_begins; state.buffer=argument;
        for(auto& binding:state.completed)if(binding.buffer==argument)binding={};
    }
}

// Normal-module metrics are copied from renderer +544/+548 at 880315C0/C4.
// Its slowdown formula reads that pair at 88123E3C/40. Observe the actual
// registers, not a later asynchronous guest-memory snapshot; never substitute
// values here. A buffer address alone is not a frame identity after reuse.
extern "C" __declspec(dllexport) void SdojLogicMetricsEvent(
    std::uint32_t module,std::uint32_t phase,std::uint32_t first,std::uint32_t second) {
    if(!REXCVAR_GET(sdoj_logic_trace) || module!=1 || phase>2)return;
    std::uint64_t generation{},logic{},source{},sequence{};
    bool parsed{};
    {
        std::lock_guard lock(gate);
        auto& state=states[module-1];
        if(phase==0) {
            state.read_logic=0;state.read_seen=true;
            for(const auto& binding:state.completed)
                if(binding.buffer==first && binding.logic>state.read_logic)
                    state.read_logic=binding.logic;
            return;
        }
        if(phase==2) {
            if(state.pending && state.thread==GetCurrentThreadId()) {
                state.slow_stat0=first; state.slow_stat1=second; state.slow_valid=true;
                state.slow_metrics_sequence=state.metrics_emitted;
            }
            return;
        }
        // The wrapper also publishes zero metrics on no-parse/held frames.
        // Do not attribute those to the last command buffer that WAS parsed.
        parsed=state.read_seen;source=state.read_logic;
        state.read_seen=false;state.read_logic=0;
        if(!state.logic || state.metrics_emitted++>=Limit)return;
        sequence=state.metrics_emitted;
        generation=state.generation;logic=state.logic;
    }
    REXLOG_INFO("SDOJ_METRICS module={} generation={} phase=rendered sequence={} parsed={} source_logic={} producer_logic={} stat0={} stat1={} no_guest_writes",
        module,generation,sequence,parsed,source,logic,first,second);
}

// Called at the reviewed gameplay wait AFTER its baseline capture and BEFORE
// publishing the completion fence. The writer still owns its immutable result;
// do not retain a span after returning or hash a GPU/Present-owned pointer.
extern "C" __declspec(dllexport) void SdojLogicParkEvent(
    std::uint32_t module,std::uint32_t buffer,std::uint32_t cursor,std::uint32_t depth) {
    if(!REXCVAR_GET(sdoj_logic_trace) || module<1 || module>2)return;
    State snapshot;
    {
        std::lock_guard lock(gate);
        auto& state=states[module-1];
        if(!state.pending || state.thread!=GetCurrentThreadId())return;
        state.pending=false;
        if(state.emitted++>=Limit)return;
        snapshot=state;
    }
    const char* issue="none";
    const auto state_addresses=CoreStateAddresses(module);
    const auto core_state=Word(state_addresses.outer),play_state=Word(state_addresses.play),
               end_request=Word(state_addresses.end);
    const bool state_valid=core_state && play_state && end_request;
    bool valid=snapshot.input_valid && snapshot.command_begins==1 && snapshot.buffer==buffer && buffer && cursor>buffer &&
        !(buffer&3) && !(cursor&3) && cursor-buffer<=0x120000 && depth && depth<=16;
    std::string hash="-";
    const auto bytes=valid ? Read(buffer,cursor-buffer) : std::nullopt;
    if(!bytes) { valid=false; issue="identity_or_mapping"; }
    else if(bytes->size()<4 || (*bytes)[bytes->size()-4]!=0xff ||
            (*bytes)[bytes->size()-3]!=0xff || (*bytes)[bytes->size()-2]!=0xff ||
            (*bytes)[bytes->size()-1]!=0xff) { valid=false; issue="missing_sentinel"; }
    if(valid) {
        std::array<std::uint8_t,32> digest{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes->data()),
                      ULONG(bytes->size()),digest.data(),ULONG(digest.size()))<0) {
            valid=false; issue="hash_failure";
        } else {
            constexpr char hex[]="0123456789abcdef";
            hash.clear(); hash.reserve(64);
            for(auto byte:digest) { hash+=hex[byte>>4]; hash+=hex[byte&15]; }
        }
    }
    if(!state_valid) {valid=false;issue="core_state_mapping";}
    if(valid) {
        std::lock_guard lock(gate);
        auto& state=states[module-1];
        if(state.generation==snapshot.generation)
            state.completed[snapshot.logic%state.completed.size()]={buffer,snapshot.logic};
    }
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    REXLOG_INFO("SDOJ_LOGIC module={} generation={} logic={} batch={} thread={} input0={:08X} input1={:08X} previous0={:08X} previous1={:08X} depth={} command_bytes={} command_sha256={} valid={} issue={} t_us={} limit={} core_state={} play_state={} end_request={} slow_stat0={} slow_stat1={} slow_metrics_sequence={} no_guest_writes not_pixels_or_scanout",
        module,snapshot.generation,snapshot.logic,snapshot.batch,snapshot.thread,
        snapshot.input.current[0],snapshot.input.current[1],snapshot.input.previous[0],snapshot.input.previous[1],
        depth,bytes ? bytes->size() : 0,hash,valid,issue,us,Limit,
        core_state?std::int64_t(*core_state):-1,play_state?std::int64_t(*play_state):-1,
        end_request?std::int64_t(*end_request):-1,
        snapshot.slow_valid?std::int64_t(snapshot.slow_stat0):-1,
        snapshot.slow_valid?std::int64_t(snapshot.slow_stat1):-1,
        snapshot.slow_valid?snapshot.slow_metrics_sequence:0);
    if(valid)Content(module,snapshot,*bytes);
}
