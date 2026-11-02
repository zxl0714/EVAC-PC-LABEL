#include "gs_trace.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cave::ps2;
constexpr size_t VramBytes=4u*1024u*1024u;
void Require(bool good,const char* text){if(!good)throw std::runtime_error(text);}
class Consumer final:public GsCommandBackend {
public:
    std::vector<unsigned> order;
    std::vector<std::pair<GifPathId,std::vector<uint8_t>>> gifs;
    std::vector<uint8_t> image,edited;
    std::array<uint64_t,4> setup{};
    uint64_t value=0;uint8_t address=0;GsFrame frame{};size_t editOffset=0;
    bool rejectField=false;
    void Initialize(std::span<const uint8_t> bytes) override{Require(bytes.size()==VramBytes && bytes[9]==7,"Initial VRAM changed");order.push_back(1);}
    void Reset() override{order.push_back(2);}
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override{gifs.emplace_back(path,std::vector<uint8_t>(bytes.begin(),bytes.end()));order.push_back(3);}
    void Register(uint8_t a,uint64_t v) override{address=a;value=v;order.push_back(4);}
    void Image(const std::array<uint64_t,4>& s,std::span<const uint8_t> bytes) override{setup=s;image.assign(bytes.begin(),bytes.end());order.push_back(5);}
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override{editOffset=offset;edited.assign(bytes.begin(),bytes.end());order.push_back(6);}
    void Frame(const GsFrame& f) override{frame=f;order.push_back(7);}
    bool TryFrame(const GsFrame& f) override{if(rejectField)return false;Frame(f);return true;}
    size_t ReadFifo(std::span<uint8_t> bytes) override{std::fill(bytes.begin(),bytes.end(),0x5c);order.push_back(8);return bytes.size();}
    void Snapshot(std::vector<uint8_t>&) override{throw std::runtime_error("Capture requested an unexpected snapshot");}
};
struct Files {
    std::filesystem::path root=std::filesystem::current_path()/
        ("trace-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files(){Require(std::filesystem::create_directory(root),"Cannot create fixture directory");}
    ~Files(){if(!std::uncaught_exceptions()){
        for(const auto& file:std::filesystem::directory_iterator(root))std::filesystem::remove(file.path());
        std::filesystem::remove(root);
    }else std::cerr<<"GS trace fixture retained at "<<root<<'\n';}
};
std::vector<uint8_t> Load(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void Save(const std::filesystem::path& path,const std::vector<uint8_t>& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
void RoundTrip(const std::filesystem::path& path) {
    auto live=std::make_unique<Consumer>();auto* source=live.get();
    GsRecordingBackend recorder(std::move(live),path);
    std::vector<uint8_t> initial(VramBytes);initial[9]=7;recorder.Initialize(initial);
    std::array<uint8_t,16> payload{};payload[9]=0x42;
    for(auto route:{GifPathId::Path1,GifPathId::Path2,GifPathId::Path3})recorder.Gif(route,payload);
    recorder.Register(0x4c,0x123456789abcdef0ull);
    recorder.Image({0x1234,0x5678,0x9012,0},payload);
    recorder.WriteVram(0x2000,payload);std::array<uint8_t,32> fifo{};recorder.ReadFifo(fifo);
    GsFrame frame{};
    frame.pmode=1;frame.smode1=2;frame.smode2=3;frame.srfsh=4;frame.synch1=5;frame.synch2=6;frame.syncv=7;
    frame.dispfb1=8;frame.display1=9;frame.dispfb2=10;frame.display2=11;
    frame.extbuf=12;frame.extdata=13;frame.extwrite=14;frame.bgcolor=15;frame.vsyncTick=16;frame.field=1;
    recorder.Frame(frame);recorder.Reset();recorder.Finish();
    Consumer replay;const auto stats=ReplayGsTrace(path,replay);
    Require(stats.finished && stats.commands==10 && stats.frames==1 && stats.bytes==std::filesystem::file_size(path),"Trace counters or end record changed");
    Require(replay.order==source->order && replay.gifs==source->gifs,"Recorded command order, paths or partial payloads changed");
    Require(replay.address==0x4c && replay.value==0x123456789abcdef0ull && replay.setup==source->setup && replay.image==source->image &&
        replay.editOffset==0x2000 && replay.edited==source->edited,"Direct register/image/VRAM shortcut changed");
    const auto& f=replay.frame;
    const std::array actual{f.pmode,f.smode1,f.smode2,f.srfsh,f.synch1,f.synch2,f.syncv,f.dispfb1,f.display1,f.dispfb2,f.display2,
        f.extbuf,f.extdata,f.extwrite,f.bgcolor,f.vsyncTick};
    for(size_t i=0;i<actual.size();++i)Require(actual[i]==i+1,"Privileged field snapshot changed");
    Require(f.field==1 && recorder.Stats().commands==stats.commands,"Field parity or capture counters changed");
    auto bytes=Load(path);const std::array<uint8_t,16> expected{'E','V','A','C','G','S','0','1',1,0,0,0,0,0,0,0};
    Require(std::equal(expected.begin(),expected.end(),bytes.begin()),"Trace header is not explicit versioned little endian");
    bool rejected=false;try{GsRecordingBackend overwrite(std::make_unique<Consumer>(),path);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected && Load(path)==bytes,"Capture overwrote existing evidence");
}
void Limits(const std::filesystem::path& root) {
    std::vector<uint8_t> initial(VramBytes);initial[9]=7;
    {
        auto live=std::make_unique<Consumer>();auto* source=live.get();
        const auto path=root/"limited.evacgs";
        GsRecordingBackend capture(std::move(live),path,VramBytes+64);
        capture.Initialize(initial);capture.Register(7,9);capture.Frame({});capture.Register(7,10);
        Require(capture.Stats().finished && capture.Stats().limited && std::filesystem::file_size(path)<=VramBytes+64,"Byte budget exceeded or did not finalize");
        Require(source->value==10 && source->order.back()==4,"Capture limit stopped the live renderer");
        Consumer replay;auto result=ReplayGsTrace(path,replay);Require(result.finished && result.frames==0 && replay.value==9,"Limited trace is not a valid prefix");
    }
    {
        auto live=std::make_unique<Consumer>();auto* source=live.get();const auto path=root/"one-field.evacgs";
        GsRecordingBackend capture(std::move(live),path,8u*1024u*1024u,1);
        capture.Initialize(initial);capture.Frame({});capture.Frame({});
        Consumer replay;auto result=ReplayGsTrace(path,replay);
        Require(result.frames==1 && capture.Stats().limited && source->order.size()==3,"Frame budget changed live flow or replay count");
    }
}
void RejectMalformed(const std::filesystem::path& root) {
    const auto good=Load(root/"full.evacgs");const auto bad=root/"bad.evacgs";
    unsigned rejected=0;
    for(unsigned variant=0;variant<7;++variant){
        auto bytes=good;
        if(variant==0)bytes[0]='?';
        if(variant==1)bytes[8]=99;
        if(variant==2)bytes.resize(15);
        if(variant==3)bytes.pop_back();
        if(variant==4)bytes[20]=0xff,bytes[21]=0xff,bytes[22]=0xff,bytes[23]=0x7f;
        if(variant==5)bytes.push_back(0);
        if(variant==6)bytes[16]=0xfe;
        Save(bad,bytes);Consumer replay;
        try{ReplayGsTrace(bad,replay);}catch(const std::runtime_error&){++rejected;}
    }
    Require(rejected==7,"Malformed/truncated trace was accepted");
    Consumer replay;bool bounded=false;try{ReplayGsTrace(root/"full.evacgs",replay,1024);}catch(const std::runtime_error&){bounded=true;}
    Require(bounded && replay.order.empty(),"File limit was checked after replay started");
}
void ExtendedBudget(const std::filesystem::path& root) {
    const auto path=root/"extended-budget.evacgs";
    GsRecordingBackend capture(std::make_unique<Consumer>(),path,MaxGsTraceBytes,1);
    std::vector<uint8_t> initial(VramBytes);initial[9]=7;
    capture.Initialize(initial);capture.Frame({});capture.Finish();
    Consumer replay;const auto stats=ReplayGsTrace(path,replay,MaxGsTraceBytes);
    Require(stats.frames==1 && stats.bytes<VramBytes+1024,"Extended budget must not preallocate a multi-gigabyte file");
    const auto rejected=root/"over-budget.evacgs";
    bool writeRejected=false,readRejected=false;
    try {GsRecordingBackend invalid(std::make_unique<Consumer>(),rejected,MaxGsTraceBytes+1,1);}
    catch(const std::invalid_argument&){writeRejected=true;}
    try {ReplayGsTrace(path,replay,MaxGsTraceBytes+1);}
    catch(const std::invalid_argument&){readRejected=true;}
    Require(writeRejected && readRejected && !std::filesystem::exists(rejected),"Diagnostic budget must remain bounded before file creation/replay");
}
void DeferredField(const std::filesystem::path& path) {
    auto live=std::make_unique<Consumer>();auto* source=live.get();
    GsRecordingBackend recorder(std::move(live),path);
    std::vector<uint8_t> initial(VramBytes);initial[9]=7;recorder.Initialize(initial);
    GsFrame field{};field.vsyncTick=23;source->rejectField=true;
    Require(!recorder.TryFrame(field) && recorder.Stats().frames==0,"Rejected field entered the capture");
    source->rejectField=false;
    Require(recorder.TryFrame(field),"Deferred field was not accepted after capacity release");
    recorder.Finish();Consumer replay;const auto result=ReplayGsTrace(path,replay);
    Require(result.frames==1 && replay.frame.vsyncTick==23 && source->order==replay.order,
            "Retried field was omitted or duplicated in the GS trace");
}
}
int main(){try{Files files;RoundTrip(files.root/"full.evacgs");Limits(files.root);RejectMalformed(files.root);ExtendedBudget(files.root);DeferredField(files.root/"deferred.evacgs");
    std::cout<<"GS capture/replay paths, shortcuts, frame state, budgets, no overwrite and malformed input: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
