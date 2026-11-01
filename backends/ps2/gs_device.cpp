#include "gs_device.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include "thread_id.hpp"
#include <cstring>
#include <stdexcept>
#include <thread>

namespace cave::ps2 {
class GsDevice::Impl {
public:
    // Destruction reverses this order: GS, device, then Vulkan context.
    Vulkan::Context context;
    Vulkan::Device device;
    ParallelGS::GSInterface gs;
    const std::thread::id owner=std::this_thread::get_id();
    std::string gpu;
    Impl() {
        Util::register_thread_index(0);
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        context.set_num_thread_indices(1);
        if(!context.init_instance_and_device(nullptr,0,nullptr,0))
            throw std::runtime_error("Cannot initialize headless Vulkan device");
        device.set_context(context);device.init_frame_contexts(2);
        if(device.get_gpu_properties().deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)
            throw std::runtime_error("PS2 graphics requires a hardware Vulkan device");
        ParallelGS::GSOptions options{};
        if(!gs.init(&device,options))throw std::runtime_error("GPU lacks required paraLLEl-GS capabilities");
        gpu=device.get_gpu_properties().deviceName;
    }
    ~Impl() {
        Util::register_thread_index(0);
        gs.flush();device.wait_idle();
    }
    void CheckOwner() const {
        if(std::this_thread::get_id()!=owner)
            throw std::logic_error("GS commands must execute on their owner thread");
    }
    void CheckRange(size_t offset,size_t bytes) const {
        CheckOwner();
        if(offset>VramBytes || bytes>VramBytes-offset)
            throw std::out_of_range("GS VRAM range exceeds 4 MiB");
    }
};
GsDevice::GsDevice():impl(std::make_unique<Impl>()){}
GsDevice::~GsDevice()=default;
const std::string& GsDevice::GpuName() const noexcept{return impl->gpu;}
void GsDevice::GifTransfer(GsGifPath path,std::span<const uint8_t> payload) {
    impl->CheckOwner();
    const auto id=static_cast<uint32_t>(path);
    if(id<1 || id>3 || payload.size()%16)
        throw std::invalid_argument("GS GIF paths are 1..3 and transfers use 16-byte units");
    impl->gs.gif_transfer(id,payload.data(),payload.size());
}
void GsDevice::WriteRegister(uint32_t address,uint64_t value) {
    impl->CheckOwner();
    if(address>=128)throw std::out_of_range("Invalid GS drawing register");
    impl->gs.write_register(static_cast<ParallelGS::RegisterAddr>(address),value);
}
void GsDevice::WriteVram(size_t offset,std::span<const uint8_t> bytes) {
    impl->CheckRange(offset,bytes.size());
    if(bytes.empty())return;
    std::memcpy(impl->gs.map_vram_write(offset,bytes.size()),bytes.data(),bytes.size());
    impl->gs.end_vram_write(offset,bytes.size());
}
void GsDevice::ReadbackVram(size_t offset,std::span<uint8_t> bytes) {
    impl->CheckRange(offset,bytes.size());
    if(!bytes.empty())std::memcpy(bytes.data(),impl->gs.map_vram_read(offset,bytes.size()),bytes.size());
}
void GsDevice::ReadTransferFifo(std::span<uint8_t> bytes) {
    impl->CheckOwner();
    if(bytes.size()%16 || bytes.size()/16>UINT32_MAX)
        throw std::invalid_argument("Invalid GS FIFO read size");
    if(!bytes.empty())impl->gs.read_transfer_fifo(bytes.data(),uint32_t(bytes.size()/16));
}
void GsDevice::Flush(){impl->CheckOwner();impl->gs.flush();}
void GsDevice::Reset(){impl->CheckOwner();impl->gs.flush();impl->gs.reset_context_state();}
}
