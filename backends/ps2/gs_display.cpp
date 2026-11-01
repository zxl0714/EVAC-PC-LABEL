#define VK_USE_PLATFORM_WIN32_KHR 1
#include <Windows.h>
#include "gs_display.h"
#include "gs_device_private.h"
#include "wsi.hpp"
#include "fence.hpp"
#include "thread_id.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace cave::ps2 {
Vulkan::Program* CreateGsPresentProgram(Vulkan::Device& device);
namespace {
class NativeWindow final : public Vulkan::WSIPlatform {
public:
    HWND handle{};
    bool closed=false;
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
        }
        return DefWindowProcW(handle,message,wparam,lparam);
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
    GsPresentTransfer transfer=GsPresentTransfer::Automatic;
    Vulkan::Program* presentProgram=nullptr;
    int loggedTransfer=-1;
    Impl(unsigned width,unsigned height,const std::wstring& title,bool vsync):window(width,height,title) {
        if(!Vulkan::Context::init_loader(nullptr))throw std::runtime_error("Vulkan loader unavailable");
        Util::register_thread_index(0);
        wsi.set_platform(&window);
        wsi.set_backbuffer_format(Vulkan::BackbufferFormat::UNORM);
        wsi.set_present_mode(vsync?Vulkan::PresentMode::SyncToVBlank:Vulkan::PresentMode::UnlockedForceTearing);
        wsi.set_present_low_latency_mode(true);
        wsi.set_extra_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if(!wsi.init_simple(1,{}))throw std::runtime_error("Cannot initialize Vulkan WSI");
        // PGS can flush several times in one game field. Allocation contexts
        // are independent of the one outstanding game-frame fence below.
        wsi.get_device().init_frame_contexts(16);
    }
    ~Impl() {
        if(pending)pending->wait();
        const auto readbacks=gs?gs->ReadbackRequests():0;
        gs.reset(); // GS must die before WSI's device and the native surface.
        std::cerr<<"[ps2-gs-present] frames="<<frames<<" readbacks="<<readbacks<<'\n';
    }
};
GsDisplay::GsDisplay(unsigned width,unsigned height,const std::wstring& title,bool vsync,GsPresentTransfer transfer)
    :impl(std::make_unique<Impl>(width,height,title,vsync)) {
    impl->transfer=transfer;
    impl->gs.reset(new GsDevice(impl->wsi.get_device()));
    ShowWindow(impl->window.handle,SW_SHOWNORMAL);
}
GsDisplay::~GsDisplay()=default;
GsDevice& GsDisplay::Device(){return *impl->gs;}
bool GsDisplay::Alive(){impl->window.poll_input();return !impl->window.closed;}
void GsDisplay::WaitPreviousFrame() {
    impl->gs->impl->CheckOwner();
    if(impl->pending){impl->pending->wait();impl->pending.reset();}
}
bool GsDisplay::Present(const GsFrame& request) {
    auto& state=*impl->gs->impl;
    state.CheckOwner();
    WaitPreviousFrame();
    if(!Alive() || !impl->wsi.begin_frame())return false;
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
    auto& registers=state.gs.get_priv_register_state();
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
    info.dst_layout=blit?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.dst_stage=blit?VK_PIPELINE_STAGE_2_BLIT_BIT:VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    info.dst_access=blit?VK_ACCESS_2_TRANSFER_READ_BIT:VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    info.adapt_to_internal_horizontal_resolution=true;
    // The renderer's vsync consumes completed GS command batches. It does not
    // flush GSInterface's pending draw/transfer tracker on behalf of callers.
    state.gs.flush();
    auto source=state.gs.vsync(info);
    auto command=device.request_command_buffer();
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
    if(!impl->wsi.end_frame())return false;
    ++impl->frames;
    return true;
}
}
