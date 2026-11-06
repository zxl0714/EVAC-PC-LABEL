#include "gs_device_private.h"
#include "gs_crash_trace.h"
#include "startup_trace.h"
#include "field_blit.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include "thread_id.hpp"
#include "command_buffer.hpp"
#include "fence.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace cave::ps2 {
GsDevice::Impl::Impl(Vulkan::Device* borrowed) : device(borrowed ? borrowed : &ownedDevice) {
    traceGif=borrowed && gs_crash::owner.load()==GetCurrentThreadId();
    Util::register_thread_index(0);
    if(!borrowed) {
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        context.set_num_thread_indices(1);
        if(!context.init_instance_and_device(nullptr,0,nullptr,0))
            throw std::runtime_error("Cannot initialize headless Vulkan device");
        ownedDevice.set_context(context);ownedDevice.init_frame_contexts(AllocationContexts);
    }
    if(device->get_gpu_properties().deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)
        throw std::runtime_error("PS2 graphics requires a hardware Vulkan device");
    ParallelGS::GSOptions options{};
    StartupTrace("gs-init-begin");
    if(!gs.init(device,options))throw std::runtime_error("GPU lacks required paraLLEl-GS capabilities");
    StartupTrace("gs-init-complete");
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
    ++workSerial;
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
Vulkan::Program* CreateGsFieldProgram(Vulkan::Device& device);
void GsDevice::EnableFieldBlitTracking(){impl->CheckOwner();impl->trackFieldBlits=true;}
bool GsDevice::IsReadyPage(uint64_t displayBuffer) const {
    return !impl->trackFieldBlits || (impl->completedBlitPage==(displayBuffer&511u) &&
        impl->blitFields[impl->completedBlitPage].generation==impl->scanoutGeneration);
}
uint64_t GsDevice::ScanoutRevision(uint64_t displayBuffer) const {
    return impl->trackFieldBlits?impl->blitFields[displayBuffer&511u].revision:impl->workSerial;
}

// Ibara's final pass scales its game texture into a display page with a
// textured four-corner fan. Its half-line raster placement identifies the
// field carried by that page, even when the EE repeats a flip.
void GsDevice::Impl::ObserveFieldBlit(unsigned path,std::span<const uint8_t> packet) {
    const auto& last=gs.get_gif_path(path);
    if(last.loop!=last.tag.NLOOP || last.tag.FLG!=ParallelGS::GIFTagBits::REGLIST ||
       last.tag.NLOOP!=1 || last.tag.NREG!=8 || last.tag.REGS!=0x53535353u)return;
    const auto& state=gs.get_register_state();
    if(state.prim.bits!=0x115u)return; // textured triangle fan, fixed UV, context 1
    const auto& ctx=state.ctx[0];
    if(ctx.scissor.desc.SCAY0)return;
    const auto phase=FieldBlitPhase(packet,ctx.xyoffset.desc.OFX,ctx.xyoffset.desc.OFY,
                                    int(ctx.scissor.desc.SCAY1)+1);
    if(phase){
        completedBlitPage=ctx.frame.desc.FBP;
        blitFields[completedBlitPage]={scanoutGeneration,*phase,workSerial};
    }
}

Vulkan::ImageHandle GsDevice::Impl::ReconstructField(Vulkan::ImageHandle field,unsigned phase,
                    VkImageLayout layout,VkPipelineStageFlags2 stage,VkAccessFlags2 access) {
    for(auto& lane:fieldHistory) {
        if(lane[0] && (lane[0]->get_width()!=field->get_width() || lane[0]->get_height()!=field->get_height())) {
            lane[0].reset();
            lane[1].reset();
        }
    }
    auto& same=fieldHistory[phase];
    auto& opposite=fieldHistory[phase^1];
    same[1]=std::move(same[0]);
    same[0]=std::move(field);
    // FastMAD compares two samples from each parity. Repeated fields replace
    // only their own parity lane, so the opposite lines remain complementary.
    const auto& previous=opposite[0]?opposite[0]:same[0];
    const auto& olderSame=same[1]?same[1]:same[0];
    const auto& olderOpposite=opposite[1]?opposite[1]:previous;
    auto imageInfo=Vulkan::ImageCreateInfo::immutable_2d_image(same[0]->get_width(),
                       2*same[0]->get_height(),VK_FORMAT_R8G8B8A8_UNORM);
    imageInfo.initial_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto output=device->create_image(imageInfo);
    auto cmd=device->request_command_buffer();
    cmd->image_barrier(*output,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,0,VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Vulkan::RenderPassInfo pass{};
    pass.num_color_attachments=1;pass.color_attachments[0]=&output->get_view();pass.store_attachments=1;
    cmd->begin_render_pass(pass);cmd->set_opaque_sprite_state();
    if(!fieldProgram)fieldProgram=CreateGsFieldProgram(*device);
    cmd->set_program(fieldProgram);
    cmd->set_texture(0,0,same[0]->get_view());cmd->set_texture(0,1,previous->get_view());
    cmd->set_texture(0,2,olderSame->get_view());cmd->set_texture(0,3,olderOpposite->get_view());
    const uint32_t push[]={phase,output->get_height()-1};
    cmd->push_constants(push,0,sizeof(push));cmd->draw(3);cmd->end_render_pass();
    cmd->image_barrier(*output,VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,layout,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,stage,access);
    device->submit(cmd);
    return output;
}

void GsDevice::GifTransfer(GsGifPath path,std::span<const uint8_t> payload) {
    impl->CheckOwner();
    const auto id=static_cast<uint32_t>(path);
    if(id<1 || id>3 || payload.size()%16)
        throw std::invalid_argument("GS GIF paths are 1..3 and transfers use 16-byte units");
    if(impl->traceGif)gs_crash::activeGif.store((uint64_t(payload.size())<<2)|id,std::memory_order_release);
    impl->BeginOperation();impl->gs.gif_transfer(id,payload.data(),payload.size());
    if(impl->traceGif)gs_crash::activeGif.store(0,std::memory_order_release);
    if(impl->trackFieldBlits && path==GsGifPath::Path2)impl->ObserveFieldBlit(id,payload);
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
    ++impl->workSerial;
    if(impl->trackFieldBlits)impl->blitFields={};
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
    ++impl->workSerial;
    impl->CheckOwner();impl->completions->Reset();impl->commandGeneration=0;
    impl->gs.flush();impl->gs.reset_context_state();
    impl->blitFields={};
    for(auto& lane:impl->fieldHistory)for(auto& field:lane)field.reset();
}
ParallelGS::ScanoutResult GsDevice::Impl::ScanoutFlushed(const GsFrame& request,VkImageLayout layout,
                                                VkPipelineStageFlags2 stage,VkAccessFlags2 access,bool currentField) {
    CheckOwner();
    auto& registers=gs.get_priv_register_state();
    using namespace ParallelGS;
    registers.pmode=Reg64<PMODEBits>(request.pmode).desc;
    registers.smode1=Reg64<SMODE1Bits>(request.smode1).desc;
    // The pinned SMODE2 declaration has oversized unused padding. Assign its
    // defined fields instead of treating that host struct as an eight-byte union.
    registers.smode2.INT=request.smode2&1u;
    registers.smode2.FFMD=(request.smode2>>1u)&1u;
    registers.smode2.DPMS=(request.smode2>>2u)&3u;
    registers.srfsh.dummy=request.srfsh;registers.synch1.dummy=request.synch1;registers.synch2.dummy=request.synch2;
    registers.dispfb1=Reg64<DISPFBBits>(request.dispfb1).desc;
    registers.display1=Reg64<DISPLAYBits>(request.display1).desc;
    registers.dispfb2=Reg64<DISPFBBits>(request.dispfb2).desc;
    registers.display2=Reg64<DISPLAYBits>(request.display2).desc;
    registers.bgcolor=Reg64<BGCOLORBits>(request.bgcolor).desc;
    registers.syncv=Reg64<SYNCVBits>(request.syncv).desc;
    registers.extbuf=Reg64<EXTBUFBits>(request.extbuf).desc;
    registers.extdata=Reg64<EXTDATABits>(request.extdata).desc;
    registers.extwrite=Reg64<EXTWRITEBits>(request.extwrite).desc;
    VSyncInfo info{};
    info.phase=request.field&1u;
    info.dst_layout=layout;info.dst_stage=stage;info.dst_access=access;
    info.adapt_to_internal_horizontal_resolution=true;
    // These 2D games render a complete framebuffer in FIELD mode. Preserve
    // each frame's 30 Hz flicker effects instead of weaving alternating shadow
    // fields together. The renderer keeps normal interlaced FRAME mode intact.
    info.force_progressive=(request.smode2&3u)==1u;
    info.skip_deinterlace=currentField;
    if(!trackFieldBlits)return gs.vsync(info);
    const auto tagged=blitFields[registers.dispfb2.FBP];
    // A field may wait behind the front page or be held across a missed flip.
    // Expire after two further drawing batches when a scene reuses these pages.
    // A held page can be scanned for any number of idle fields.
    const bool composite=!currentField && (request.smode2&3u)==3u && (request.pmode&3u)==2u &&
        tagged.generation && scanoutGeneration-tagged.generation<=2;
    const auto work=gs.consume_flush_stats();
    if(work.num_render_passes || work.num_copies)++scanoutGeneration;
    if(!composite) {
        for(auto& lane:fieldHistory)for(auto& field:lane)field.reset();
        return gs.vsync(info);
    }
    info.skip_deinterlace=true;
    info.dst_layout=VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    info.dst_stage=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;info.dst_access=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    auto result=gs.vsync(info);
    if(result.image && result.interlaced) {
        result.image=ReconstructField(std::move(result.image),tagged.phase,layout,stage,access);
        result.interlace_phase=tagged.phase;
    }else if(result.image && layout!=info.dst_layout) {
        auto cmd=device->request_command_buffer();
        cmd->image_barrier(*result.image,info.dst_layout,layout,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT,stage,access);
        device->submit(cmd);
    }
    return result;
}

bool GsDevice::RenderField(const GsFrame& frame) {
    impl->CheckOwner();WaitField();
    impl->gs.flush();
    auto result=impl->ScanoutFlushed(frame,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        frame.fixedFieldDrawing && (frame.smode2&3u)==3u);
    impl->lastField=std::move(result.image);
    auto command=impl->device->request_command_buffer();impl->device->submit(command,&impl->fieldFence);
    return bool(impl->lastField);
}
void GsDevice::WaitField() {
    impl->CheckOwner();if(impl->fieldFence){impl->fieldFence->wait();impl->fieldFence.reset();}
}
void GsDevice::SetOfflineReplay(bool enabled) {
    impl->CheckOwner();impl->gs.set_signal_interface(enabled?nullptr:impl.get());
}
GsDrawRegisters GsDevice::InspectDrawRegisters() const {
    impl->CheckOwner();const auto& state=impl->gs.get_register_state();
    return {{state.ctx[0].frame.bits,state.ctx[1].frame.bits},
            {state.ctx[0].tex0.bits,state.ctx[1].tex0.bits},
            {state.ctx[0].alpha.bits,state.ctx[1].alpha.bits},state.prim.bits};
}

}
