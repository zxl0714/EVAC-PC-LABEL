// Authored native observer test: no ROM, game loop, window, GPU or real input.
#include "diagnostics/logic_trace.h"
#include "diagnostics/input_trace_policy.h"
#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

extern "C" bool SdojLogicTraceEnabled();
extern "C" void SdojLogicParkEvent(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
extern "C" void SdojLogicMetricsEvent(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
extern "C" bool SdojLogicContentEnabled();
extern "C" void SdojLogicTilemapEvent(std::uint32_t,std::uint32_t,std::uint32_t,
    std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
namespace {
unsigned checks{};
void Check(bool value,const char* text) { ++checks; if(!value)throw std::runtime_error(text); }
std::vector<std::string> Rows(const std::shared_ptr<rex::LogCaptureSink>& sink,const char* prefix="SDOJ_LOGIC ") {
    rex::FlushLogging();
    std::vector<rex::LogEntry> entries; sink->CopyEntries(entries);
    std::vector<std::string> rows;
    for(const auto& entry:entries)if(entry.text.find(prefix)!=std::string::npos)rows.push_back(entry.text);
    return rows;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"expected isolated fixture directory");
        const auto root=std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root/"empty-data");
        auto capture=std::make_shared<rex::LogCaptureSink>();
        rex::InitLoggingEarly(); auto logging=rex::BuildLogConfig(nullptr,"info",{});
        logging.extra_sinks.push_back(capture); rex::InitLogging(logging);
        Check(!SdojLogicTraceEnabled(),"logic observer must be default-off");
        Check(!SdojLogicContentEnabled(),"tilemap observer must be default-off");
        SdojLogicTilemapEvent(1,63,63,0,0,0,0);
        Check(Rows(capture,"SDOJ_TILEMAP ").empty(),"disabled tilemap capture reads no memory");
        SdojLogicTraceEvent(1,0,0); SdojLogicParkEvent(1,1,5,1);
        SdojLogicMetricsEvent(1,0,1,0); SdojLogicMetricsEvent(1,1,111,222);
        Check(Rows(capture,"SDOJ_METRICS ").empty(),"disabled metrics observer emits nothing");
        Check(Rows(capture).empty(),"disabled observer emits no records or reads invalid guest addresses");
        Check(Rows(capture,"SDOJ_LOGIC_CONTENT ").empty(),"exact content capture is default-off");
        Check(rex::cvar::SetFlagByName("sdoj_logic_trace","true"),"enable fixture diagnostic");
        Check(rex::cvar::SetFlagByName("sdoj_logic_content_from","1"),"select exact content frame");
        Check(rex::cvar::SetFlagByName("sdoj_logic_content_count","1"),"bound exact content capture");
        rex::Runtime runtime(root/"empty-data",root/"profile",{},root/"cache",{});
        rex::RuntimeConfig config; config.tool_mode=true; config.kernel_init=rex::kernel::InitializeKernel;
        Check(runtime.Setup(std::move(config))==0,"non-playable fixture runtime");
        auto* memory=runtime.memory();
        auto* heap=memory->LookupHeap(0x88870000);
        Check(heap && heap->AllocFixed(0x88870000,0x20000,0x10000,3,3),"synthetic input pages");
        auto* core_heap=memory->LookupHeap(0x886A0000);
        Check(core_heap && core_heap->AllocFixed(0x886A0000,0x20000,0x10000,3,3),"synthetic core-state pages");
        const auto buffer=memory->SystemHeapAlloc(128);
        Check(buffer!=0,"synthetic command allocation");
        auto* command=memory->TranslateVirtual<std::uint8_t*>(buffer);
        auto put=[&](std::uint32_t address,std::uint32_t value) {
            const auto swapped=__builtin_bswap32(value);
            std::memcpy(memory->TranslateVirtual(address),&swapped,4);
        };
        std::memset(memory->TranslateVirtual(0x88870000),0,0x20000);
        std::memset(memory->TranslateVirtual(0x886A0000),0,0x20000);
        std::memset(command,0,128);
        put(sdoj::diagnostics::CoreInputBlock(1)+12,0x10);
        put(sdoj::diagnostics::CoreInputBlock(2)+12,0x20);
        put(0x886ACB48,7);put(0x886ACC30,3);put(0x886B4798,0);
        put(0x886B6600,10);put(0x886B66E8,8);put(0x886BCCC0,1);
        const auto* core_pages=memory->TranslateVirtual<std::uint8_t*>(0x886A0000);
        const std::vector<std::uint8_t> original_core(core_pages,core_pages+0x20000);
        const auto* input_pages=memory->TranslateVirtual<std::uint8_t*>(0x88870000);
        const std::vector<std::uint8_t> original_input(input_pages,input_pages+0x20000);
        put(buffer,0x11223344); put(buffer+4,0xaabbccdd); put(buffer+8,0xffffffff);
        const std::vector<std::uint8_t> original(command,command+128);
        auto begin=[&](unsigned module) {
            SdojLogicTraceEvent(module,1,0); SdojLogicTraceEvent(module,5,buffer);
        };
        SdojLogicTraceEvent(1,0,0); begin(1);
        const auto map=memory->SystemHeapAlloc(64*64*8);
        Check(map!=0,"synthetic tilemap allocation");
        auto* map_bytes=memory->TranslateVirtual<std::uint8_t*>(map);
        std::memset(map_bytes,0,64*64*8);
        put(map+(63*64+63)*8,0x80000001); put(map+63*64*8,0x00200020);
        put(map+63*8,0x01020304);
        const std::vector<std::uint8_t> original_map(map_bytes,map_bytes+64*64*8);
        std::thread foreign_tilemap([&] { SdojLogicTilemapEvent(map,63,63,24,0x500,0x808080,0x88120D64); });
        foreign_tilemap.join();
        Check(Rows(capture,"SDOJ_TILEMAP ").empty(),"foreign thread cannot emit gameplay tilemap provenance");
        SdojLogicTilemapEvent(map,63,63,24,0x500,0x808080,0x88120D64);
        const auto tiles=Rows(capture,"SDOJ_TILEMAP ");
        Check(tiles.size()==9 && tiles[0].find("source_row=63 source_column=63 xy=00000018")!=std::string::npos,
              "selected native tilemap emits nine complete source rows");
        Check(tiles[0].find("row_offset=0 cells=8000000100200020")!=std::string::npos &&
              tiles[1].find("row_offset=1 cells=0102030400000000")!=std::string::npos,
              "tilemap row and column wrapping matches the native 64x64 indices");
        Check(std::equal(original_map.begin(),original_map.end(),map_bytes),"tilemap observation never writes source cells");
        SdojLogicMetricsEvent(1,2,111,222);
        SdojLogicParkEvent(1,buffer,buffer+12,1);
        SdojLogicTilemapEvent(map,63,63,24,0x500,0x808080,0x88120D64);
        Check(Rows(capture,"SDOJ_TILEMAP ").size()==9,"completed logic cannot emit more tilemap provenance");
        auto rows=Rows(capture);
        Check(rows.size()==1 && rows[0].find("input0=00000010")!=std::string::npos &&
              rows[0].find("valid=true")!=std::string::npos && rows[0].find("command_bytes=12")!=std::string::npos,
              "real observer records native input and completed extent");
        Check(rows[0].find("command_sha256=a3f9ad53252f3e30b344806ed5ce37073cd18d8a1b0d8393de3fa12bf8558640")!=std::string::npos,"known authored-byte SHA-256");
        const auto content=Rows(capture,"SDOJ_LOGIC_CONTENT ");
        Check(content.size()==1 && content[0].find("logic=1 bytes=12 offset=0 hex=11223344aabbccddffffffff")!=std::string::npos,
              "selected completed batch retains its exact byte order and extent");
        Check(rows[0].find("core_state=7 play_state=3 end_request=0")!=std::string::npos,
              "normal module reads its separately reviewed state addresses");
        Check(rows[0].find("slow_stat0=111 slow_stat1=222")!=std::string::npos,
              "actual slowdown operands belong to their pending logic frame");
        SdojLogicMetricsEvent(1,0,buffer,0); SdojLogicMetricsEvent(1,1,333,444);
        Check(Rows(capture,"SDOJ_METRICS ").back().find("source_logic=1 producer_logic=1 stat0=333 stat1=444")!=std::string::npos,
              "rendered metrics bind to the known completed buffer generation");
        SdojLogicMetricsEvent(1,1,0,0);
        Check(Rows(capture,"SDOJ_METRICS ").back().find("parsed=false source_logic=0")!=std::string::npos,
              "a no-parse wrapper must not reuse the previous frame identity");
        SdojLogicMetricsEvent(1,0,buffer+4,0); SdojLogicMetricsEvent(1,1,333,444);
        Check(Rows(capture,"SDOJ_METRICS ").back().find("source_logic=0")!=std::string::npos,
              "unknown rendered buffer is reported instead of guessed");
        Check(std::equal(original.begin(),original.end(),command),"observer never writes command data");
        SdojLogicParkEvent(1,buffer,buffer+12,1);
        Check(Rows(capture).size()==1,"same completion cannot emit twice");
        begin(1);
        SdojLogicTilemapEvent(1,63,63,24,0x500,0x808080,0x88120D64);
        Check(Rows(capture,"SDOJ_TILEMAP ").size()==9,"unselected frame does not read the supplied invalid tilemap");
        put(buffer+8,0); SdojLogicParkEvent(1,buffer,buffer+12,1);
        Check(Rows(capture).back().find("issue=missing_sentinel")!=std::string::npos,"partial commands rejected");
        Check(Rows(capture).back().find("slow_stat0=-1 slow_stat1=-1")!=std::string::npos,
              "a new logic frame never inherits a previous slowdown observation");
        put(buffer+8,0xffffffff);
        begin(1); SdojLogicParkEvent(1,buffer+4,buffer+12,1);
        Check(Rows(capture).back().find("valid=false")!=std::string::npos,"foreign completed buffer rejected");
        begin(1); SdojLogicParkEvent(1,buffer,buffer+12,0);
        Check(Rows(capture).back().find("valid=false")!=std::string::npos,"unknown slowdown depth rejected");
        begin(1); SdojLogicTraceEvent(1,5,buffer); SdojLogicParkEvent(1,buffer,buffer+12,1);
        Check(Rows(capture).back().find("valid=false")!=std::string::npos,"multiple batches cannot masquerade as one logic result");
        SdojLogicTraceEvent(1,1,0);
        const auto before_wrong_thread=Rows(capture).size();
        std::thread foreign([&] {SdojLogicTraceEvent(1,5,buffer);SdojLogicParkEvent(1,buffer,buffer+12,1);});
        foreign.join();
        Check(Rows(capture).size()==before_wrong_thread,"another thread cannot complete the pending gameplay frame");
        SdojLogicTraceEvent(1,5,buffer); SdojLogicParkEvent(1,buffer,buffer+12,2);
        Check(Rows(capture).back().find("valid=true")!=std::string::npos &&
              Rows(capture).back().find("depth=2")!=std::string::npos,"matching writer keeps native slowdown depth");
        SdojLogicMetricsEvent(1,0,buffer,0); SdojLogicMetricsEvent(1,1,555,666);
        Check(Rows(capture,"SDOJ_METRICS ").back().find("source_logic=6 ")!=std::string::npos,
              "reused command address selects the newest completed logical identity");
        begin(1); SdojLogicTraceEvent(1,0,0); const auto before_reload=Rows(capture).size();
        SdojLogicParkEvent(1,buffer,buffer+12,1);
        Check(Rows(capture).size()==before_reload,"module reset cannot emit a stale pending frame");
        Check(Rows(capture,"SDOJ_LOGIC_CONTENT ").size()==1,
              "invalid, duplicate and outside-window completions emit no command content");
        SdojLogicTraceEvent(2,0,0); begin(2); SdojLogicParkEvent(2,buffer,buffer+12,1);
        Check(Rows(capture).back().find("module=2")!=std::string::npos &&
              Rows(capture).back().find("input0=00000020")!=std::string::npos,"second module has independent input state");
        Check(Rows(capture).back().find("core_state=10 play_state=8 end_request=1")!=std::string::npos,
              "arrange module reads its own state and end-request flag");
        Check(std::equal(original.begin(),original.end(),command),"all observation paths preserve command bytes");
        Check(std::equal(original_input.begin(),original_input.end(),input_pages),"all observation paths preserve both input blocks");
        Check(std::equal(original_core.begin(),original_core.end(),core_pages),"observer never writes the core state");
        memory->SystemHeapFree(map); memory->SystemHeapFree(buffer); runtime.Shutdown();
        std::cout<<checks<<" native logic observer checks passed; no game or devices executed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"check="<<checks<<" "<<error.what()<<'\n';return 1;}
}
