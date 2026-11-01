#include "gs_device_private.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include "thread_id.hpp"
#include "fence.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace cave::ps2 {
GsDevice::Impl::Impl(Vulkan::Device* borrowed) : device(borrowed ? borrowed : &ownedDevice) {
    Util::register_thread_index(0);
    if(!borrowed) {
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        context.set_num_thread_indices(1);
        if(!context.init_instance_and_device(nullptr,0,nullptr,0))
            throw std::runtime_error("Cannot initialize headless Vulkan device");
        ownedDevice.set_context(context);ownedDevice.init_frame_contexts(2);
    }
    if(device->get_gpu_properties().deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)
        throw std::runtime_error("PS2 graphics requires a hardware Vulkan device");
    ParallelGS::GSOptions options{};
    if(!gs.init(device,options))throw std::runtime_error("GPU lacks required paraLLEl-GS capabilities");
    gs.set_signal_interface(this);
    gpu=device->get_gpu_properties().deviceName;
}
GsDevice::Impl::~Impl() {
    Util::register_thread_index(0);
    gs.flush();device->wait_idle();
}
void GsDevice::Impl::CheckOwner() const {
    if(std::this_thread::get_id()!=owner)
        throw std::logic_error("GS commands must execute on their owner thread");
}
void GsDevice::Impl::CheckRange(size_t offset,size_t bytes) const {
    CheckOwner();
    if(offset>VramBytes || bytes>VramBytes-offset)
        throw std::out_of_range("GS VRAM range exceeds 4 MiB");
}
void GsDevice::Impl::BeginOperation() {
    activeGeneration=commandGeneration?commandGeneration:completions->Generation();
}
void GsDevice::Impl::CompleteMarker() {
    // The Generic queue also waits on PGS's preceding async-transfer submission.
    // A fence after flush completes this marker's work without waiting idle for
    // the entire device or requiring a frame/window to be submitted.
    gs.flush();
    auto command=device->request_command_buffer();
    Vulkan::Fence fence;device->submit(command,&fence);
    if(waitPump) {
        while(!fence->wait_timeout(1000000))waitPump();
    }else fence->wait();
}
bool GsDevice::Impl::on_signal(uint64_t payload) {
    if(activeGeneration!=completions->Generation())return false;
    CompleteMarker();completions->Signal(activeGeneration,payload,waitPump);return false;
}
bool GsDevice::Impl::on_finish(uint64_t) {
    if(activeGeneration!=completions->Generation())return false;
    CompleteMarker();completions->Finish(activeGeneration);return false;
}
bool GsDevice::Impl::on_label(uint64_t payload) {
    completions->Label(activeGeneration,payload);return false;
}
GsDevice::GsDevice():impl(std::make_unique<Impl>()){}
GsDevice::GsDevice(Vulkan::Device& device):impl(std::make_unique<Impl>(&device)){}
GsDevice::~GsDevice()=default;
const std::string& GsDevice::GpuName() const noexcept{return impl->gpu;}
uint64_t GsDevice::ReadbackRequests() const{impl->CheckOwner();return impl->readbacks;}
std::shared_ptr<GsCompletionMailbox> GsDevice::Completions() const{return impl->completions;}
void GsDevice::SetCommandGeneration(uint64_t generation){impl->CheckOwner();impl->commandGeneration=generation;}
void GsDevice::SetWaitPump(std::function<void()> pump){impl->CheckOwner();impl->waitPump=std::move(pump);}
void GsDevice::GifTransfer(GsGifPath path,std::span<const uint8_t> payload) {
    impl->CheckOwner();
    const auto id=static_cast<uint32_t>(path);
    if(id<1 || id>3 || payload.size()%16)
        throw std::invalid_argument("GS GIF paths are 1..3 and transfers use 16-byte units");
    impl->BeginOperation();impl->gs.gif_transfer(id,payload.data(),payload.size());
}
void GsDevice::WriteRegister(uint32_t address,uint64_t value) {
    impl->CheckOwner();
    if(address>=128)throw std::out_of_range("Invalid GS drawing register");
    impl->BeginOperation();impl->gs.write_register(static_cast<ParallelGS::RegisterAddr>(address),value);
}
void GsDevice::UploadImage(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) {
    impl->CheckOwner();
    if((setup[3]&3u)!=0u)throw std::invalid_argument("Native image upload must be host-to-local");
    for(unsigned i=0;i<4;++i)WriteRegister(0x50u+i,setup[i]);
    // HWREG is a public GS operation, independent of the partial tag state of
    // each original GIF path. The normal DMA path still uses bulk raw packets.
    for(size_t offset=0;offset<bytes.size();offset+=8u) {
        uint64_t word{};
        std::memcpy(&word,bytes.data()+offset,std::min<size_t>(8,bytes.size()-offset));
        impl->gs.write_register(ParallelGS::RegisterAddr::HWREG,word);
    }
}
void GsDevice::WriteVram(size_t offset,std::span<const uint8_t> bytes) {
    impl->CheckRange(offset,bytes.size());
    if(bytes.empty())return;
    std::memcpy(impl->gs.map_vram_write(offset,bytes.size()),bytes.data(),bytes.size());
    impl->gs.end_vram_write(offset,bytes.size());
}
void GsDevice::ReadbackVram(size_t offset,std::span<uint8_t> bytes) {
    impl->CheckRange(offset,bytes.size());
    if(!bytes.empty()){
        ++impl->readbacks;
        std::memcpy(bytes.data(),impl->gs.map_vram_read(offset,bytes.size()),bytes.size());
    }
}
void GsDevice::ReadTransferFifo(std::span<uint8_t> bytes) {
    impl->CheckOwner();
    if(bytes.size()%16 || bytes.size()/16>UINT32_MAX)
        throw std::invalid_argument("Invalid GS FIFO read size");
    if(!bytes.empty())impl->gs.read_transfer_fifo(bytes.data(),uint32_t(bytes.size()/16));
}
void GsDevice::Flush(){impl->CheckOwner();impl->gs.flush();}
void GsDevice::Reset(){
    impl->CheckOwner();impl->completions->Reset();impl->commandGeneration=0;
    impl->gs.flush();impl->gs.reset_context_state();
}
}
