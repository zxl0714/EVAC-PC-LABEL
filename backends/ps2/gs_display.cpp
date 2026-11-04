#define VK_USE_PLATFORM_WIN32_KHR 1
#include <Windows.h>
#include "gs_display.h"
#include "frame_trace.h"
#include "gs_device_private.h"
#include "wsi.hpp"
#include "fence.hpp"
#include "thread_id.hpp"
#include "timer.hpp"
#include "gs_pixel_trace.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace cave::ps2 {
Vulkan::Program* CreateGsPresentProgram(Vulkan::Device& device);
namespace {
class NativeWindow final : public Vulkan::WSIPlatform {
public:
    HWND handle{};
    bool closed=false;
    bool fullscreen=false;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    std::function<void(unsigned,bool)> keyHandler;
    std::exception_ptr inputFailure;
    std::chrono::steady_clock::time_point nextMessagePoll{};
    NativeWindow(unsigned width,unsigned height,const std::wstring& title) {
        WNDCLASSW info{};
        info.lpfnWndProc=Procedure;info.hInstance=GetModuleHandleW(nullptr);
        info.lpszClassName=L"EVAC.PS2.Vulkan";info.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        if(!RegisterClassW(&info) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register PS2 window class");
        RECT rectangle{0,0,LONG(width),LONG(height)};
        AdjustWindowRect(&rectangle,WS_OVERLAPPEDWINDOW,FALSE);
        handle=CreateWindowExW(0,info.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
            nullptr,nullptr,info.hInstance,this);
        if(!handle)throw std::runtime_error("Cannot create PS2 Vulkan window");
    }
    ~NativeWindow() override{if(handle)DestroyWindow(handle);}
    VkSurfaceKHR create_surface(VkInstance instance,VkPhysicalDevice) override {
        VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        info.hinstance=GetModuleHandleW(nullptr);info.hwnd=handle;
        VkSurfaceKHR surface{};
        if(vkCreateWin32SurfaceKHR(instance,&info,nullptr,&surface)!=VK_SUCCESS)
            throw std::runtime_error("Cannot create Vulkan window surface");
        return surface;
    }
    std::vector<const char*> get_instance_extensions() override {
        return {VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    }
    VkFormat get_preferred_format() override{return VK_FORMAT_B8G8R8A8_UNORM;}
    uint32_t get_surface_width() override{RECT size{};GetClientRect(handle,&size);return uint32_t(size.right);}
    uint32_t get_surface_height() override{RECT size{};GetClientRect(handle,&size);return uint32_t(size.bottom);}
    bool alive(Vulkan::WSI&) override{return !closed;}
    void poll_input() override {
        // Command bursts can request thousands of polls between two fields.
        // Bound native message polling to 500 us; SDL retains its own cadence.
        const auto now=std::chrono::steady_clock::now();
        if(now<nextMessagePoll)return;
        nextMessagePoll=now+std::chrono::microseconds(500);
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT){closed=true;continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
    }
    void poll_input_async(Granite::InputTrackerHandler*) override{}
    uintptr_t get_native_window() override{return reinterpret_cast<uintptr_t>(handle);}
    static LRESULT CALLBACK Procedure(HWND handle,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self=reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<NativeWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self) {
            if(message==WM_CLOSE){self->closed=true;return 0;}
            if(message==WM_SIZE)self->resize=true;
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP) {
                try {
                    const bool down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
                    if(wparam==VK_F9 && down && !(lparam&(1ll<<30)))pixel_trace::Arm();
                    if(wparam==VK_F11 && down && !(lparam&(1ll<<30)))self->ToggleFullscreen();
                    else if(self->keyHandler)self->keyHandler(unsigned(wparam),down);
                }catch(...){self->inputFailure=std::current_exception();self->closed=true;}
            }
        }
        return DefWindowProcW(handle,message,wparam,lparam);
    }
    void ToggleFullscreen() {
        if(!fullscreen) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if(!GetWindowPlacement(handle,&placement) ||
               !GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&monitor))
                throw std::runtime_error("Cannot select fullscreen monitor");
            SetWindowLongPtrW(handle,GWL_STYLE,GetWindowLongPtrW(handle,GWL_STYLE)&~WS_OVERLAPPEDWINDOW);
            SetWindowPos(handle,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,
                monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
                SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        }else {
            SetWindowLongPtrW(handle,GWL_STYLE,GetWindowLongPtrW(handle,GWL_STYLE)|WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(handle,&placement);
            SetWindowPos(handle,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        }
        fullscreen=!fullscreen;
    }
};
}
class GsDisplay::Impl {
public:
    NativeWindow window;
    Vulkan::WSI wsi;
    std::unique_ptr<GsDevice> gs;
    Vulkan::Fence pending;
    uint64_t frames=0;
    int64_t tracedGpuWait=0;
    GsPresentTransfer transfer=GsPresentTransfer::Automatic;
    Vulkan::Program* presentProgram=nullptr;
    int loggedTransfer=-1;
    uint64_t lastFeedback=0;
    pixel_trace::Capture pixels;
    Impl(unsigned width,unsigned height,const std::wstring& title,bool vsync):window(width,height,title) {
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        Util::register_thread_index(0);
        wsi.set_platform(&window);
        wsi.set_backbuffer_format(Vulkan::BackbufferFormat::UNORM);
        wsi.set_present_mode(vsync?Vulkan::PresentMode::SyncToVBlank:Vulkan::PresentMode::UnlockedForceTearing);
        wsi.set_present_low_latency_mode(true);
        const auto* feedback=_wgetenv(L"EVAC_PS2_PRESENT_FEEDBACK");
        if(input_trace::Enabled() && feedback && std::wcscmp(feedback,L"1")==0)
            wsi.set_enable_timing_feedback(true);
        wsi.set_extra_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if(!wsi.init_simple(1,{}))throw std::runtime_error("Cannot initialize Vulkan WSI");
        // PGS can flush several times in one game field. Allocation contexts
        // are independent of the one outstanding game-frame fence below.
        wsi.get_device().init_frame_contexts(GsDevice::AllocationContexts);
    }
    ~Impl() {
        if(pending)pending->wait();
        try{pixels.Save(wsi.get_device());}
        catch(const std::exception& error){std::cerr<<"[ps2-pixel-trace] "<<error.what()<<'\n';}
        const auto readbacks=gs?gs->ReadbackRequests():0;
        gs.reset(); // GS must die before WSI's device and the native surface.
        std::cerr<<"[ps2-gs-present] frames="<<frames<<" readbacks="<<readbacks<<'\n';
    }
};
GsDisplay::GsDisplay(unsigned width,unsigned height,const std::wstring& title,bool vsync,GsPresentTransfer transfer)
    :impl(std::make_unique<Impl>(width,height,title,vsync)) {
    impl->transfer=transfer;
    impl->gs.reset(new GsDevice(impl->wsi.get_device()));
    impl->gs->SetWaitPump([this]{
        if(!Alive())throw GsWindowClosed();
    });
    ShowWindow(impl->window.handle,SW_SHOWNORMAL);
}
GsDisplay::~GsDisplay()=default;
GsDevice& GsDisplay::Device(){return *impl->gs;}
bool GsDisplay::Alive(){
    impl->window.poll_input();
    if(impl->window.inputFailure)std::rethrow_exception(impl->window.inputFailure);
    return !impl->window.closed;
}
bool GsDisplay::Focused() const{return GetForegroundWindow()==impl->window.handle;}
void GsDisplay::SetKeyHandler(std::function<void(unsigned,bool)> handler){impl->window.keyHandler=std::move(handler);}
void GsDisplay::ToggleFullscreen(){impl->window.ToggleFullscreen();}
void GsDisplay::WaitPreviousFrame() {
    impl->gs->impl->CheckOwner();
    if(impl->pending){
        const auto start=frame_trace::rows?frame_trace::Stamp():0;
        impl->pending->wait();impl->pending.reset();
        if(start)impl->tracedGpuWait+=frame_trace::Stamp()-start;
    }
}
bool GsDisplay::Present(const GsFrame& request,bool currentFieldScanout) {
    auto& state=*impl->gs->impl;
    state.CheckOwner();
    input_trace::Record(input_trace::Kind::PresentStart,request.vsyncTick,request.dispfb1,request.dispfb2);
    auto* trace=frame_trace::Get(request.vsyncTick);
    if(trace)trace->presentStart=frame_trace::Stamp();
    WaitPreviousFrame();
    if(trace){trace->gpuWait=impl->tracedGpuWait;impl->tracedGpuWait=0;}
    // Texture uploads can retain Granite command buffers between GS packets.
    // WSI begins a new allocation context and waits for all recorded buffers
    // to be submitted. Drain PGS before that boundary, rather than waiting on
    // our own pending upload inside begin_frame(). No device-wide idle wait.
    impl->gs->Flush();
    if(trace)trace->flushed=frame_trace::Stamp();
    if(!Alive() || !impl->wsi.begin_frame())return false;
    if(input_trace::Enabled()) {
        Vulkan::PresentationStats stats{};
        if(impl->wsi.get_presentation_stats(stats) && stats.feedback_present_id!=impl->lastFeedback) {
            impl->lastFeedback=stats.feedback_present_id;
            input_trace::Record(input_trace::Kind::Feedback,request.vsyncTick,stats.feedback_present_id,
                                stats.gpu_done_ts,stats.present_done_ts);
        }
        if(impl->frames%120==0)input_trace::Record(input_trace::Kind::ClockLink,request.vsyncTick,
                                                 uint64_t(Util::get_current_time_nsecs()));
    }
    if(trace)trace->acquired=frame_trace::Stamp();
    auto& device=impl->wsi.get_device();
    const auto& destination=device.get_swapchain_view().get_image();
    const bool blit=impl->transfer==GsPresentTransfer::Automatic &&
        (destination.get_create_info().usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
        device.image_format_is_supported(destination.get_create_info().format,VK_FORMAT_FEATURE_2_BLIT_DST_BIT) &&
        device.image_format_is_supported(VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_FEATURE_2_BLIT_SRC_BIT);
    if(impl->loggedTransfer!=int(blit)) {
        std::cerr<<"[ps2-gs-present] transfer="<<(blit?"gpu-blit":"gpu-sampled")<<'\n';
        impl->loggedTransfer=int(blit);
    }
    // Ibara draws a complete 224-line picture for each interlaced FRAME
    // field. Scaling the current field keeps moving sprites and the title
    // together instead of weaving in lines from an earlier game state.
    const bool currentField=currentFieldScanout && (request.smode2&3u)==3u;
    const auto sourceLayout=blit?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto sourceStage=blit?VK_PIPELINE_STAGE_2_BLIT_BIT:VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const auto sourceAccess=blit?VK_ACCESS_2_TRANSFER_READ_BIT:VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    auto source=state.Scanout(request,currentField?VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:sourceLayout,
        currentField?VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT:sourceStage,
        currentField?VK_ACCESS_2_SHADER_SAMPLED_READ_BIT:sourceAccess,currentField);
    if(trace)trace->scanned=frame_trace::Stamp();
    auto command=device.request_command_buffer();
    if(currentField && source.image)
        command->image_barrier(*source.image,VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,sourceLayout,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,
            sourceStage,sourceAccess);
    if(source.image)impl->pixels.Copy(device,*command,*source.image,request.vsyncTick,blit);
    const auto width=destination.get_width(),height=destination.get_height();
    unsigned targetWidth=width,targetHeight=width*3u/4u;
    if(targetHeight>height){targetHeight=height;targetWidth=height*4u/3u;}
    const VkOffset3D offset{int32_t((width-targetWidth)/2u),int32_t((height-targetHeight)/2u),0};
    const VkOffset3D extent{int32_t(targetWidth),int32_t(targetHeight),1};
    if(blit) {
        command->swapchain_touch_in_stages(VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        command->image_barrier(destination,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,0,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearValue black{};
        command->clear_image(destination,black);
        if(source.image) {
            command->image_barrier(destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
            const VkOffset3D sourceExtent{int32_t(source.image->get_width()),int32_t(source.image->get_height()),1};
            command->blit_image(destination,*source.image,offset,extent,{0,0,0},sourceExtent,0,0,0,0,1,VK_FILTER_NEAREST);
        }
        command->image_barrier(destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_NONE,0);
    }else {
        command->swapchain_touch_in_stages(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        command->image_barrier(destination,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,0,VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        Vulkan::RenderPassInfo pass{};
        pass.num_color_attachments=1;pass.color_attachments[0]=&device.get_swapchain_view();
        pass.clear_attachments=1;pass.store_attachments=1;
        command->begin_render_pass(pass);command->set_opaque_sprite_state();
        if(source.image) {
            if(!impl->presentProgram)impl->presentProgram=CreateGsPresentProgram(device);
            command->set_program(impl->presentProgram);
            command->set_texture(0,0,source.image->get_view(),Vulkan::StockSampler::NearestClamp);
            const VkViewport viewport{float(offset.x),float(offset.y),float(extent.x),float(extent.y),0,1};
            command->set_viewport(viewport);command->draw(3);
        }
        command->end_render_pass();
        command->image_barrier(destination,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_2_NONE,0);
    }
    device.submit(command,&impl->pending);
    impl->pixels.Submitted(request.vsyncTick);
    input_trace::Record(input_trace::Kind::Submit,request.vsyncTick);
    if(trace)trace->submitted=frame_trace::Stamp();
    if(!impl->wsi.end_frame())return false;
    if(input_trace::Enabled())input_trace::Record(input_trace::Kind::PresentEnd,request.vsyncTick,
                                                 impl->wsi.get_last_submitted_present_id());
    if(trace)trace->presented=frame_trace::Stamp();
    ++impl->frames;
    return true;
}
}
