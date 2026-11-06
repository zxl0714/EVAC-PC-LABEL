#define VK_USE_PLATFORM_WIN32_KHR 1
#include <Windows.h>
#include "gs_display.h"
#include "startup_trace.h"
#include "gs_crash_trace.h"
#include "frame_trace.h"
#include "gs_device_private.h"
#include "wsi.hpp"
#include "fence.hpp"
#include "thread_id.hpp"
#include "timer.hpp"
#include "gs_pixel_trace.h"
#include "reflex.h"
#include "../../common/deadline_wait.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace cave::ps2 {
using gs_crash::Stage;
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
    Reflex reflex;
    const bool amdRequested;
    PFN_vkAntiLagUpdateAMD amdUpdate{};
    VkDevice amdDevice{};
    std::atomic<uint64_t> amdField{};
    NativeWindow(unsigned width,unsigned height,const std::wstring& title,bool enable,bool amd)
        :reflex(enable),amdRequested(amd) {
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
    void event_device_created(Vulkan::Device* gpu) override {
        if (!amdRequested) return;
        if (gpu->get_gpu_properties().vendorID==0x1002 && gpu->get_device_features().anti_lag_features.antiLag) {
            amdDevice=gpu->get_device();
            amdUpdate=gpu->get_device_table().vkAntiLagUpdateAMD;
        }
        std::fprintf(stderr,"AMD_ANTI_LAG backend=ps2-vulkan requested=true active=%s\n",amdUpdate?"true":"false");
    }
    void BeforeRead(uint64_t field,unsigned budgetUs) {
        if (!amdUpdate) { reflex.BeforeRead(field,budgetUs); return; }
        if (field && amdField.compare_exchange_strong(field,0)) {
            VkAntiLagDataAMD input{VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD};
            input.mode=VK_ANTI_LAG_MODE_ON_AMD;
            amdUpdate(amdDevice,&input);
        }
    }
    void event_swapchain_created(Vulkan::Device* gpu,VkSwapchainKHR swapchain,unsigned,unsigned,float,size_t,
                                 VkFormat,VkColorSpaceKHR,VkSurfaceTransformFlagBitsKHR) override {
        if(gpu)reflex.Connect(gpu->get_device(),gpu->get_device_table(),swapchain,
            gpu->get_gpu_properties().vendorID==0x10de && gpu->get_device_features().supports_low_latency2_nv);
    }
    void event_swapchain_destroyed() override {amdField=0;reflex.Disconnect();}
    void event_device_destroyed() override {amdUpdate=nullptr;amdDevice=VK_NULL_HANDLE;reflex.ReleaseDevice();}
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
    ParallelGS::ScanoutResult lastReadyImage{};
    cave::DeadlineWait presentTimer;
    std::mutex presentMutex;
    std::condition_variable presentWake;
    uint64_t frames=0;
    int64_t tracedGpuWait=0;
    Vulkan::Program* presentProgram=nullptr;
    uint64_t pendingFrame=0;
    void Mark(Stage stage) const {gs_crash::Mark(stage,frames);}
    uint64_t lastFeedback=0;
    pixel_trace::Capture pixels;
    Impl(unsigned width,unsigned height,const std::wstring& title,bool vsync,bool reflex,bool amd)
        :window(width,height,title,reflex,amd) {
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        Util::register_thread_index(0);
        wsi.set_platform(&window);
        wsi.set_backbuffer_format(Vulkan::BackbufferFormat::UNORM);
        wsi.set_present_mode(vsync?Vulkan::PresentMode::SyncToVBlank:Vulkan::PresentMode::UnlockedForceTearing);
        wsi.set_present_low_latency_mode(true);
        const auto* timingFeedback=_wgetenv(L"EVAC_PS2_PRESENT_FEEDBACK");
        if(input_trace::Enabled() && timingFeedback && std::wcscmp(timingFeedback,L"1")==0)
            wsi.set_enable_timing_feedback(true);
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
        lastReadyImage.image.reset();
        gs.reset(); // GS must die before WSI's device and the native surface.
        std::cerr<<"[ps2-gs-present] frames="<<frames<<" readbacks="<<readbacks<<'\n';
    }
};
GsDisplay::GsDisplay(unsigned width,unsigned height,const std::wstring& title,bool vsync,bool reflex,bool amd)
    :impl(std::make_unique<Impl>(width,height,title,vsync,reflex,amd)) {
    const auto& gpu=impl->wsi.get_device().get_gpu_properties();
    gs_crash::Start();
    impl->Mark(Stage::Init);
    std::fprintf(stderr,"[ps2-gs-present] transfer=gpu-sampled gpu=%s device=%04x driver_raw=%u render_thread=%lu\n",
                 gpu.deviceName,gpu.deviceID,gpu.driverVersion,GetCurrentThreadId());
    impl->gs.reset(new GsDevice(impl->wsi.get_device()));
    impl->gs->SetWaitPump([this]{
        if(!Alive())throw GsWindowClosed();
    });
    ShowWindow(impl->window.handle,SW_SHOWNORMAL);
}
GsDisplay::~GsDisplay()=default;
GsDevice& GsDisplay::Device(){return *impl->gs;}
void GsDisplay::BeforeInput(uint64_t field,unsigned budgetUs){impl->window.BeforeRead(field,budgetUs);}
void GsDisplay::SimulationReady(uint64_t field){impl->window.reflex.SimulationReady(field);}
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
        gs_crash::Mark(Stage::WaitFence,impl->pendingFrame);
        impl->pending->wait();
        // wait() also returns on device errors. Only record a confirmed signal.
        gs_crash::Mark(impl->pending->wait_timeout(0)?Stage::FenceSignaled:Stage::FenceUnconfirmed,impl->pendingFrame);
        impl->pending.reset();
        if(start)impl->tracedGpuWait+=frame_trace::Stamp()-start;
    }
}
bool GsDisplay::Present(const GsFrame& request,bool repeatCompletedImage) {
    impl->window.reflex.BeginOutput();
    auto& state=*impl->gs->impl;
    state.CheckOwner();
    input_trace::Record(input_trace::Kind::PresentStart,request.vsyncTick,request.dispfb1,request.dispfb2);
    auto* trace=frame_trace::Get(request.vsyncTick);
    if(trace){trace->presentStart=frame_trace::Stamp();trace->presentedReady=request.Prepared();trace->repeated=repeatCompletedImage;}
    WaitPreviousFrame();
    if(trace){trace->gpuWait=impl->tracedGpuWait;impl->tracedGpuWait=0;}
    // Texture uploads can retain Granite command buffers between GS packets.
    // WSI begins a new allocation context and waits for all recorded buffers
    // to be submitted. Drain PGS before that boundary, rather than waiting on
    // our own pending upload inside begin_frame(). No device-wide idle wait.
    impl->Mark(Stage::FlushGs);
    impl->gs->Flush();
    if(trace)trace->flushed=frame_trace::Stamp();
    impl->Mark(Stage::Acquire);
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
    const bool smooth=!request.fixedFieldDrawing && (request.smode2&3u)==3u;
    const bool currentField=request.fixedFieldDrawing && (request.smode2&3u)==3u;
    const bool reuse=repeatCompletedImage && impl->lastReadyImage.image;
    impl->Mark(reuse?Stage::ReuseImage:Stage::Scanout);
    auto source=reuse?impl->lastReadyImage:state.ScanoutFlushed(request,VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,currentField);
    impl->Mark(source.image?Stage::SourceImage:Stage::EmptyImage);
    // A held display page can span two CRT fields. Reuse its completed pixels
    // without running the deinterlacer twice on the same source field.
    if(state.trackFieldBlits && request.Prepared())impl->lastReadyImage=source;
    else impl->lastReadyImage.image.reset();
    if(trace)trace->scanned=frame_trace::Stamp();
    auto command=device.request_command_buffer();
    if(source.image)
        impl->pixels.Copy(device,*command,*source.image,request,source.interlace_phase);
    // Granite's swapchain render pass discards on clear, waits on acquire
    // at color output and transitions its attachment to PRESENT on exit.
    Vulkan::RenderPassInfo pass{};
    pass.num_color_attachments=1;pass.color_attachments[0]=&device.get_swapchain_view();
    pass.clear_attachments=1;pass.store_attachments=1;
    impl->Mark(Stage::BeginPass);
    command->begin_render_pass(pass);command->set_opaque_sprite_state();
    if(source.image) {
        impl->Mark(Stage::BindProgram);
        if(!impl->presentProgram)impl->presentProgram=CreateGsPresentProgram(device);
        command->set_program(impl->presentProgram);
        command->set_texture(0,0,source.image->get_view(),
                             smooth?Vulkan::StockSampler::LinearClamp:Vulkan::StockSampler::NearestClamp);
        const auto width=destination.get_width(),height=destination.get_height();
        unsigned targetWidth=width,targetHeight=width*3u/4u;
        if(targetHeight>height){targetHeight=height;targetWidth=height*4u/3u;}
        const VkViewport viewport{float((width-targetWidth)/2u),float((height-targetHeight)/2u),
                                  float(targetWidth),float(targetHeight),0,1};
        command->set_viewport(viewport);
        impl->Mark(Stage::Draw);
        command->draw(3);
    }
    impl->Mark(Stage::EndPass);
    command->end_render_pass();
    impl->Mark(Stage::Submit);
    device.submit(command,&impl->pending);
    impl->pendingFrame=impl->frames;
    impl->Mark(Stage::Submitted);
    impl->pixels.Submitted(request.vsyncTick);
    input_trace::Record(input_trace::Kind::Submit,request.vsyncTick);
    if(trace)trace->submitted=frame_trace::Stamp();
    if(request.Prepared()) {
        // Pace ready frames on the existing guest clock. If drawing ran late,
        // present immediately rather than slipping to another display slot.
        const auto now=std::chrono::steady_clock::now();
        if(request.presentAfter>now && request.presentAfter-now<std::chrono::milliseconds(17)) {
            std::unique_lock lock(impl->presentMutex);
            impl->presentTimer.Until(lock,request.presentAfter,[]{return false;},impl->presentWake);
        }
    }
    impl->Mark(Stage::Present);
    if(!impl->wsi.end_frame())return false;
    impl->Mark(Stage::Presented);
    if(impl->frames==0)StartupTrace("first-present-returned");
    if(input_trace::Enabled())input_trace::Record(input_trace::Kind::PresentEnd,request.vsyncTick,
                                                 impl->wsi.get_last_submitted_present_id());
    if(trace)trace->presented=frame_trace::Stamp();
    if(impl->window.amdUpdate)impl->window.amdField.store(request.vsyncTick+1);
    impl->window.reflex.AfterPresent(impl->wsi.get_last_submitted_present_id(),request.vsyncTick);
    ++impl->frames;
    impl->Mark(Stage::BetweenFrames);
    return true;
}
}
