#include "gs_device_sink.h"
#include "gs_completion_mailbox.h"
#include <stdexcept>

namespace cave::ps2 {
GsDeviceSink::GsDeviceSink(GsDevice& gs,std::function<void(const GsFrame&)> onFrame,
                         std::function<void()> onBeginFrame)
    :device(gs),frame(std::move(onFrame)),beginFrame(std::move(onBeginFrame)) {
    if(!frame)throw std::invalid_argument("GS command consumer requires a frame destination");
}
void GsDeviceSink::BeginCommand(uint64_t generation){device.SetCommandGeneration(generation);}
GsCompletions GsDeviceSink::CollectCompletions(){return device.Completions()->Take();}
void GsDeviceSink::AcknowledgeSignal(){device.Completions()->AcknowledgeSignal();}
void GsDeviceSink::AcknowledgeStatus(uint32_t mask){device.Completions()->AcknowledgeStatus(mask);}
void GsDeviceSink::BeforeCommand() {
    if(beginNeeded) {
        if(beginFrame)beginFrame();
        beginNeeded=false;
    }
}
void GsDeviceSink::Initialize(std::span<const uint8_t> vram) {
    if(vram.size()!=GsDevice::VramBytes)throw std::invalid_argument("GS initialization requires 4 MiB VRAM");
    BeforeCommand();device.WriteVram(0,vram);
}
void GsDeviceSink::Reset(){BeforeCommand();device.Reset();}
void GsDeviceSink::Gif(GifPathId path,std::span<const uint8_t> bytes) {
    BeforeCommand();device.GifTransfer(static_cast<GsGifPath>(path),bytes);
}
void GsDeviceSink::GifBurst(std::span<const GifTransferRange> ranges,std::span<const uint8_t> bytes) {
    BeforeCommand();
    const bool observeDirect = device.TracksFieldBlits();
    while (!ranges.empty()) {
        const auto path = ranges.front().path;
        size_t size = ranges.front().size;
        ranges = ranges.subspan(1);
        // PATH2 also feeds the display-field blit observer, which uses the
        // original transfers. The other native GS paths are byte streams.
        while ((!observeDirect || path != GifPathId::Path2) && !ranges.empty() && ranges.front().path == path) {
            size += ranges.front().size;
            ranges = ranges.subspan(1);
        }
        device.GifTransfer(static_cast<GsGifPath>(path), bytes.first(size));
        bytes = bytes.subspan(size);
    }
}
void GsDeviceSink::Register(uint8_t address,uint64_t value){BeforeCommand();device.WriteRegister(address,value);}
void GsDeviceSink::Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes){BeforeCommand();device.UploadImage(setup,bytes);}
size_t GsDeviceSink::ReadFifo(std::span<uint8_t> bytes){BeforeCommand();device.ReadTransferFifo(bytes);return bytes.size();}
void GsDeviceSink::Snapshot(std::vector<uint8_t>& bytes){BeforeCommand();bytes.resize(GsDevice::VramBytes);device.ReadbackVram(0,bytes);}
void GsDeviceSink::WriteVram(size_t offset,std::span<const uint8_t> bytes){BeforeCommand();device.WriteVram(offset,bytes);}
void GsDeviceSink::Frame(const GsFrame& request){BeforeCommand();frame(request);beginNeeded=true;}
}
