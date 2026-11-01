#include "gs_device_sink.h"
#include <stdexcept>

namespace cave::ps2 {
GsDeviceSink::GsDeviceSink(GsDevice& gs,std::function<void(const GsFrame&)> onFrame,
                         std::function<void()> onBeginFrame)
    :device(gs),frame(std::move(onFrame)),beginFrame(std::move(onBeginFrame)) {
    if(!frame)throw std::invalid_argument("GS command consumer requires a frame destination");
}
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
void GsDeviceSink::Register(uint8_t address,uint64_t value){BeforeCommand();device.WriteRegister(address,value);}
void GsDeviceSink::Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes){BeforeCommand();device.UploadImage(setup,bytes);}
size_t GsDeviceSink::ReadFifo(std::span<uint8_t> bytes){BeforeCommand();device.ReadTransferFifo(bytes);return bytes.size();}
void GsDeviceSink::Snapshot(std::vector<uint8_t>& bytes){BeforeCommand();bytes.resize(GsDevice::VramBytes);device.ReadbackVram(0,bytes);}
void GsDeviceSink::WriteVram(size_t offset,std::span<const uint8_t> bytes){BeforeCommand();device.WriteVram(offset,bytes);}
void GsDeviceSink::Frame(const GsFrame& request){BeforeCommand();frame(request);beginNeeded=true;}
}
