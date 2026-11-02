#include "gs_trace.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace cave::ps2 {
namespace {
constexpr uint64_t VramBytes=4u*1024u*1024u,MaxRecord=8u*1024u*1024u+256u;
constexpr std::array<uint8_t,8> Magic{'E','V','A','C','G','S','0','1'};
enum class Op:uint32_t {End=0,Initialize=1,Reset=2,Gif=3,Register=4,Image=5,WriteVram=6,Frame=7,Fifo=8};
using Blob=std::vector<uint8_t>;
void U32(Blob& bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i)));}
void U64(Blob& bytes,uint64_t value){for(unsigned i=0;i<8;++i)bytes.push_back(uint8_t(value>>(8*i)));}
void Append(Blob& bytes,std::span<const uint8_t> payload){bytes.insert(bytes.end(),payload.begin(),payload.end());}
std::array<uint64_t*,16> Fields(GsFrame& f) {
    return {&f.pmode,&f.smode1,&f.smode2,&f.srfsh,&f.synch1,&f.synch2,&f.syncv,
        &f.dispfb1,&f.display1,&f.dispfb2,&f.display2,&f.extbuf,&f.extdata,&f.extwrite,&f.bgcolor,&f.vsyncTick};
}
class Cursor {
public:
    std::span<const uint8_t> bytes;
    explicit Cursor(std::span<const uint8_t> source):bytes(source){}
    std::span<const uint8_t> Take(size_t count) {
        if(count>bytes.size())throw std::runtime_error("Truncated GS trace record");
        auto result=bytes.first(count);bytes=bytes.subspan(count);return result;
    }
    uint32_t U32(){auto data=Take(4);uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(data[i])<<(8*i);return value;}
    uint64_t U64(){auto data=Take(8);uint64_t value=0;for(unsigned i=0;i<8;++i)value|=uint64_t(data[i])<<(8*i);return value;}
    void End()const{if(!bytes.empty())throw std::runtime_error("Unexpected bytes in GS trace record");}
};
Blob Read(std::ifstream& file,size_t count) {
    Blob bytes(count);if(count && !file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(count)))
        throw std::runtime_error("Truncated GS trace file");return bytes;
}
void CheckGif(uint32_t path,size_t bytes) {
    if(path<1 || path>3 || bytes%16)throw std::runtime_error("Invalid GS trace GIF path or size");
}
}
class GsRecordingBackend::Impl {
public:
    std::unique_ptr<GsCommandBackend> target;
    struct CloseFile {void operator()(std::FILE* file)const{if(file)std::fclose(file);}};
    std::unique_ptr<std::FILE,CloseFile> file;
    const uint64_t maxBytes,maxFrames;
    GsTraceStats stats;
    bool initialized=false;
    Impl(std::unique_ptr<GsCommandBackend> consumer,const std::filesystem::path& path,uint64_t bytes,uint64_t frames)
        :target(std::move(consumer)),maxBytes(bytes),maxFrames(frames) {
        if(!target || bytes<VramBytes+32 || bytes>MaxGsTraceBytes || !frames)
            throw std::invalid_argument("Invalid GS capture target or limit");
        file.reset(_wfopen(path.c_str(),L"wbx"));
        if(!file)throw std::runtime_error("Cannot create a new GS trace file");
        Blob header(Magic.begin(),Magic.end());U32(header,1);U32(header,0);Write(header);
    }
    ~Impl(){try{Finish();}catch(...){std::fputs("[ps2-gs-capture] Cannot finalize trace\n",stderr);}}
    void Write(std::span<const uint8_t> bytes) {
        if(!bytes.empty() && std::fwrite(bytes.data(),1,bytes.size(),file.get())!=bytes.size())
            throw std::runtime_error("Cannot write GS trace");stats.bytes+=bytes.size();
    }
    void Finish() {
        if(stats.finished)return;
        Blob end;U32(end,uint32_t(Op::End));U32(end,0);Write(end);
        if(std::fflush(file.get()))throw std::runtime_error("Cannot flush GS trace");stats.finished=true;
    }
    bool Record(Op op,std::span<const uint8_t> payload) {
        if(stats.finished)return false;
        if(!initialized && op!=Op::Initialize)throw std::logic_error("GS trace must start at initialization");
        if(payload.size()>MaxRecord)throw std::length_error("GS trace record exceeds 8 MiB");
        if(payload.size()+16>maxBytes-stats.bytes){stats.limited=true;Finish();return false;}
        Blob header;U32(header,uint32_t(op));U32(header,uint32_t(payload.size()));Write(header);Write(payload);
        ++stats.commands;return true;
    }
    void RecordFrame(const GsFrame& request) {
        if(stats.finished)return;
        Blob record;auto copy=request;for(auto* value:Fields(copy))U64(record,*value);U32(record,request.field);
        if(Record(Op::Frame,record)) {
            ++stats.frames;
            if(std::fflush(file.get()))throw std::runtime_error("Cannot flush GS field capture");
            if(stats.frames>=maxFrames){stats.limited=true;Finish();}
        }
    }
};
GsRecordingBackend::GsRecordingBackend(std::unique_ptr<GsCommandBackend> target,const std::filesystem::path& path,uint64_t bytes,uint64_t frames)
    :impl(std::make_unique<Impl>(std::move(target),path,bytes,frames)){}
GsRecordingBackend::~GsRecordingBackend()=default;
GsTraceStats GsRecordingBackend::Stats() const{return impl->stats;}
void GsRecordingBackend::Finish(){impl->Finish();}
void GsRecordingBackend::BeginCommand(uint64_t generation){impl->target->BeginCommand(generation);}
GsCompletions GsRecordingBackend::CollectCompletions(){return impl->target->CollectCompletions();}
void GsRecordingBackend::AcknowledgeStatus(uint32_t mask){impl->target->AcknowledgeStatus(mask);}
void GsRecordingBackend::AcknowledgeSignal(){impl->target->AcknowledgeSignal();}
void GsRecordingBackend::Initialize(std::span<const uint8_t> bytes) {
    if(impl->initialized || bytes.size()!=VramBytes)throw std::logic_error("Capture requires one complete initial GS VRAM");
    impl->Record(Op::Initialize,bytes);impl->initialized=true;impl->target->Initialize(bytes);
}
void GsRecordingBackend::Reset(){impl->Record(Op::Reset,{});impl->target->Reset();}
void GsRecordingBackend::Gif(GifPathId path,std::span<const uint8_t> bytes) {
    CheckGif(uint32_t(path),bytes.size());
    if(!impl->stats.finished){Blob record;U32(record,uint32_t(path));Append(record,bytes);impl->Record(Op::Gif,record);}
    impl->target->Gif(path,bytes);
}
void GsRecordingBackend::Register(uint8_t address,uint64_t value) {
    if(address>=128)throw std::out_of_range("Invalid captured GS register");
    if(!impl->stats.finished){Blob record;U32(record,address);U64(record,value);impl->Record(Op::Register,record);}
    impl->target->Register(address,value);
}
void GsRecordingBackend::Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) {
    if(!impl->stats.finished){Blob record;for(auto value:setup)U64(record,value);Append(record,bytes);impl->Record(Op::Image,record);}
    impl->target->Image(setup,bytes);
}
size_t GsRecordingBackend::ReadFifo(std::span<uint8_t> bytes) {
    if(bytes.size()>MaxRecord-8)throw std::length_error("Captured FIFO request exceeds 8 MiB");
    const auto actual=impl->target->ReadFifo(bytes);
    if(actual>bytes.size())throw std::runtime_error("GS FIFO consumer exceeded its capacity");
    if(!impl->stats.finished){Blob record;U32(record,uint32_t(bytes.size()));U32(record,uint32_t(actual));Append(record,bytes.first(actual));impl->Record(Op::Fifo,record);}
    return actual;
}
void GsRecordingBackend::Snapshot(std::vector<uint8_t>& bytes){impl->target->Snapshot(bytes);}
void GsRecordingBackend::WriteVram(size_t offset,std::span<const uint8_t> bytes) {
    if(offset>VramBytes || bytes.size()>VramBytes-offset)throw std::out_of_range("Invalid captured VRAM range");
    if(!impl->stats.finished){Blob record;U64(record,offset);Append(record,bytes);impl->Record(Op::WriteVram,record);}
    impl->target->WriteVram(offset,bytes);
}
void GsRecordingBackend::Frame(const GsFrame& request) {
    if(request.field>1)throw std::invalid_argument("Invalid captured GS field parity");
    impl->RecordFrame(request);
    impl->target->Frame(request);
}
bool GsRecordingBackend::TryFrame(const GsFrame& request) {
    if(request.field>1)throw std::invalid_argument("Invalid captured GS field parity");
    if(!impl->target->TryFrame(request))return false;
    impl->RecordFrame(request);return true;
}

class GsTraceWorkload::Impl {
public:
    struct Command {
        Op op{};Blob data;uint32_t code=0,actual=0;uint64_t value=0;
        std::array<uint64_t,4> setup{};GsFrame frame{};
    };
    std::vector<Command> commands;
    Blob fifo;
    GsTraceStats stats;
    Impl(const std::filesystem::path& path,uint64_t maxBytes) {
        if(maxBytes>MaxGsTraceBytes)throw std::invalid_argument("GS replay limit exceeds 4096 MiB");
        const auto size=std::filesystem::file_size(path);
        if(size<24 || size>maxBytes)throw std::runtime_error("GS trace file exceeds size limits");
        std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open GS trace");
        const auto header=Read(file,16);Cursor preamble(header);
        if(!std::ranges::equal(preamble.Take(8),Magic) || preamble.U32()!=1 || preamble.U32()!=0)
            throw std::runtime_error("Unsupported GS trace header");
        stats.bytes=16;bool initialized=false;uint32_t fifoSize=0;
        while(stats.bytes<size) {
            const auto raw=Read(file,8);Cursor packet(raw);Command command;
            command.op=Op(packet.U32());const auto length=packet.U32();stats.bytes+=8;
            if(length>MaxRecord || length>size-stats.bytes)throw std::runtime_error("Invalid GS trace record size");
            command.data=Read(file,length);stats.bytes+=length;Cursor data(command.data);
            if(command.op==Op::End) {
                if(length || stats.bytes!=size || !initialized)throw std::runtime_error("Invalid GS trace end");
                fifo.resize(fifoSize);stats.finished=true;return;
            }
            if(!initialized && command.op!=Op::Initialize)throw std::runtime_error("GS trace has no initial state");
            switch(command.op) {
            case Op::Initialize:
                if(initialized || length!=VramBytes)throw std::runtime_error("Invalid initial GS VRAM");
                data.Take(length);initialized=true;break;
            case Op::Reset:break;
            case Op::Gif:
                command.code=data.U32();CheckGif(command.code,data.bytes.size());data.Take(data.bytes.size());break;
            case Op::Register:
                command.code=data.U32();command.value=data.U64();
                if(command.code>=128)throw std::runtime_error("Invalid GS trace register");break;
            case Op::Image:
                for(auto& value:command.setup)value=data.U64();
                if((command.setup[3]&3u)!=0)throw std::runtime_error("Invalid GS trace image direction");
                data.Take(data.bytes.size());break;
            case Op::WriteVram:
                command.value=data.U64();
                if(command.value>VramBytes || data.bytes.size()>VramBytes-command.value)throw std::runtime_error("Invalid GS trace VRAM range");
                data.Take(data.bytes.size());break;
            case Op::Frame:
                for(auto* value:Fields(command.frame))*value=data.U64();command.frame.field=data.U32();
                if(command.frame.field>1)throw std::runtime_error("Invalid GS trace field parity");
                ++stats.frames;break;
            case Op::Fifo:
                command.code=data.U32();command.actual=data.U32();
                if(command.code>MaxRecord || command.actual>command.code || command.actual!=data.bytes.size())
                    throw std::runtime_error("Invalid GS trace FIFO reply");
                fifoSize=std::max(fifoSize,command.code);data.Take(command.actual);break;
            default:throw std::runtime_error("Unknown GS trace opcode");
            }
            data.End();
            if(command.op==Op::Register || command.op==Op::Frame || command.op==Op::Reset)Blob{}.swap(command.data);
            if(commands.size()>=1000000)throw std::runtime_error("GS trace exceeds one million commands");
            commands.push_back(std::move(command));++stats.commands;
        }
        throw std::runtime_error("GS trace has no complete end record");
    }
};
GsTraceWorkload::GsTraceWorkload(const std::filesystem::path& path,uint64_t maxBytes)
    :impl(std::make_unique<Impl>(path,maxBytes)){}
GsTraceWorkload::~GsTraceWorkload()=default;
GsTraceStats GsTraceWorkload::Stats() const{return impl->stats;}
GsTraceStats GsTraceWorkload::Replay(GsCommandBackend& target) {
    for(const auto& command:impl->commands) {
        const std::span<const uint8_t> data(command.data);
        switch(command.op) {
        case Op::Initialize:target.Initialize(data);break;
        case Op::Reset:target.Reset();break;
        case Op::Gif:target.Gif(GifPathId(command.code),data.subspan(4));break;
        case Op::Register:target.Register(uint8_t(command.code),command.value);break;
        case Op::Image:target.Image(command.setup,data.subspan(32));break;
        case Op::WriteVram:target.WriteVram(size_t(command.value),data.subspan(8));break;
        case Op::Frame:target.Frame(command.frame);break;
        case Op::Fifo: {
            const auto result=std::span<uint8_t>(impl->fifo).first(command.code);
            const auto got=target.ReadFifo(result);const auto expected=data.subspan(8);
            if(got!=command.actual || !std::equal(expected.begin(),expected.end(),result.begin()))
                throw std::runtime_error("GS replay FIFO data differs from capture");break;
        }
        default:throw std::logic_error("Invalid decoded GS trace command");
        }
    }
    return impl->stats;
}
GsTraceStats ReplayGsTrace(const std::filesystem::path& path,GsCommandBackend& target,uint64_t maxBytes) {
    GsTraceWorkload workload(path,maxBytes);return workload.Replay(target);
}
}
