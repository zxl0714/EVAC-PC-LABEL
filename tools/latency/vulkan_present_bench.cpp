// Standalone WSI micro benchmark. No ROM, gameplay, OS input injection, driver
// settings, display-mode changes or administrator rights are needed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <windows.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "vulkan_present_feedback.h"
#include "vulkan_present_wait.h"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {
bool closing=false;
struct WindowOwner {
    HWND value{};
    ~WindowOwner() {if(value) DestroyWindow(value);}
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_CLOSE) {closing=true; return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
void Require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
struct WaitClock {
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    WaitClock() { if(!timer) timer=CreateWaitableTimerW(nullptr,FALSE,nullptr); Require(timer,"Cannot create frame timer"); }
    ~WaitClock(){CloseHandle(timer);}
    void Until(std::uint64_t target) {
        const auto now=evac_latency::FeedbackQpc();
        if(now>=target) return;
        LARGE_INTEGER due{};
        due.QuadPart=-static_cast<LONGLONG>((target-now)*10'000'000/evac_latency::FeedbackFrequency());
        if(!due.QuadPart) return;
        Require(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE),"Cannot arm frame timer");
        WaitForSingleObject(timer,INFINITE);
    }
};
}

int main(int argc,char** argv) try {
    std::filesystem::path output;
    vk::PresentModeKHR mode=vk::PresentModeKHR::eFifo;
    unsigned seconds=15,width=1280,height=720;
    bool fullscreen=false,feedback_enabled=true,own_dc=false,bootstrap_fifo=false,present_wait=false;
    for(int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--fullscreen") fullscreen=true;
        else if(option=="--no-feedback") feedback_enabled=false;
        else if(option=="--own-dc") own_dc=true;
        else if(option=="--bootstrap-fifo") bootstrap_fifo=true;
        else if(option=="--present-wait") present_wait=true;
        else if(option=="--output" && i+1<argc) output=argv[++i];
        else if(option=="--seconds" && i+1<argc) seconds=std::stoul(argv[++i]);
        else if(option=="--mode" && i+1<argc) {
            const std::string name=argv[++i];
            if(name=="fifo") mode=vk::PresentModeKHR::eFifo;
            else if(name=="mailbox") mode=vk::PresentModeKHR::eMailbox;
            else if(name=="immediate") mode=vk::PresentModeKHR::eImmediate;
            else throw std::runtime_error("Unknown presentation mode");
        } else throw std::runtime_error("Use --output PATH --mode fifo|mailbox|immediate [--seconds 1..60] [--fullscreen] [--no-feedback] [--own-dc] [--bootstrap-fifo]");
    }
    Require(!output.empty() && seconds>=1 && seconds<=60,"Fresh output and 1..60 seconds required");
    Require(!present_wait || !feedback_enabled,"Use --no-feedback with --present-wait");
    Require(!std::filesystem::exists(output),"Fresh output required");
    Require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ||
            GetLastError()==ERROR_ACCESS_DENIED,"Cannot establish benchmark DPI awareness");
    const auto loader=LoadLibraryW(L"vulkan-1.dll");
    Require(loader,"No Vulkan loader");
    const auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
    Require(get,"No Vulkan instance loader");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(get);
    const vk::ApplicationInfo application{.pApplicationName="EVAC Vulkan present micro bench",.apiVersion=VK_API_VERSION_1_3};
    const std::array instance_extensions{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                       VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME};
    // Declared first so Vulkan surface/instance lifetimes end before HWND.
    WindowOwner owned_window;
    auto instance=vk::createInstanceUnique({.pApplicationInfo=&application,
        .enabledExtensionCount=static_cast<unsigned>(instance_extensions.size()),
        .ppEnabledExtensionNames=instance_extensions.data()});
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=module;wc.lpszClassName=L"EVACVulkanPresentBench";
    wc.style=own_dc?CS_OWNDC:0;
    Require(RegisterClassW(&wc),"Cannot register benchmark window");
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    RECT bounds=monitor.rcMonitor;
    const DWORD style=fullscreen?WS_POPUP:WS_OVERLAPPEDWINDOW;
    if(!fullscreen) {
        bounds={0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        AdjustWindowRect(&bounds,style,FALSE);
        const auto w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
        bounds.left=monitor.rcWork.left+((monitor.rcWork.right-monitor.rcWork.left)-w)/2;
        bounds.top=monitor.rcWork.top+((monitor.rcWork.bottom-monitor.rcWork.top)-h)/2;
        bounds.right=bounds.left+w;bounds.bottom=bounds.top+h;
    }
    const auto mode_name=vk::to_string(mode);
    const auto title=L"EVAC Vulkan present bench - "+std::wstring(mode_name.begin(),mode_name.end());
    HWND window=CreateWindowExW(0,wc.lpszClassName,title.c_str(),style,bounds.left,bounds.top,
        bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,module,nullptr);
    Require(window,"Cannot create benchmark window");
    owned_window.value=window;
    ShowWindow(window,SW_SHOW); UpdateWindow(window);
    auto surface=instance->createWin32SurfaceKHRUnique({.hinstance=module,.hwnd=window});
    auto devices=instance->enumeratePhysicalDevices();
    Require(!devices.empty(),"No GPU");
    auto physical=devices.front();
    for(auto candidate:devices)
        if(candidate.getProperties().deviceType==vk::PhysicalDeviceType::eDiscreteGpu) {physical=candidate;break;}
    const auto properties=physical.getProperties();
    const auto supported=physical.enumerateDeviceExtensionProperties();
    std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if(present_wait) for(auto name:{VK_KHR_PRESENT_ID_EXTENSION_NAME,VK_KHR_PRESENT_WAIT_EXTENSION_NAME})
        device_extensions.push_back(name);
    if(feedback_enabled) for(auto name:{VK_KHR_PRESENT_ID_2_EXTENSION_NAME,
            VK_EXT_PRESENT_TIMING_EXTENSION_NAME,VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME})
        device_extensions.push_back(name);
    for(auto name:device_extensions)
        Require(std::ranges::any_of(supported,[&](const auto& p){return std::string_view(p.extensionName.data())==name;}),name);
    if(feedback_enabled) {
        const auto features=physical.getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDevicePresentId2FeaturesKHR,vk::PhysicalDevicePresentTimingFeaturesEXT>();
        Require(features.get<vk::PhysicalDevicePresentId2FeaturesKHR>().presentId2 &&
                features.get<vk::PhysicalDevicePresentTimingFeaturesEXT>().presentTiming,"Missing timing features");
    }
    if(present_wait) {
        const auto features=physical.getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDevicePresentIdFeaturesKHR,vk::PhysicalDevicePresentWaitFeaturesKHR>();
        Require(features.get<vk::PhysicalDevicePresentIdFeaturesKHR>().presentId &&
                features.get<vk::PhysicalDevicePresentWaitFeaturesKHR>().presentWait,"Missing present-wait features");
    }
    const auto families=physical.getQueueFamilyProperties();
    unsigned family=~0u;
    for(unsigned i=0;i<families.size();++i)
        if((families[i].queueFlags & vk::QueueFlagBits::eGraphics) && physical.getSurfaceSupportKHR(i,*surface)) {family=i;break;}
    Require(family!=~0u && families[family].timestampValidBits,"No timestamp-capable presentation queue");
    const float priority=1;
    const vk::DeviceQueueCreateInfo queue_info{.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    vk::PhysicalDevicePresentTimingFeaturesEXT timing_feature{.presentTiming=true};
    const vk::PhysicalDevicePresentId2FeaturesKHR id_feature{.pNext=&timing_feature,.presentId2=true};
    vk::PhysicalDevicePresentWaitFeaturesKHR wait_feature{.presentWait=true};
    const vk::PhysicalDevicePresentIdFeaturesKHR wait_id_feature{.pNext=&wait_feature,.presentId=true};
    const void* extra_features=feedback_enabled?static_cast<const void*>(&id_feature):
        present_wait?static_cast<const void*>(&wait_id_feature):nullptr;
    auto device=physical.createDeviceUnique({.pNext=extra_features,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,
        .enabledExtensionCount=static_cast<unsigned>(device_extensions.size()),.ppEnabledExtensionNames=device_extensions.data()});
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
    auto queue=device->getQueue(family,0);
    const auto modes=physical.getSurfacePresentModesKHR(*surface);
    Require(std::ranges::find(modes,mode)!=modes.end(),"Requested mode unsupported");
    const auto formats=physical.getSurfaceFormatsKHR(*surface);
    const auto chosen=std::ranges::find_if(formats,[](const auto& f){return f.format==vk::Format::eB8G8R8A8Unorm;});
    Require(chosen!=formats.end(),"No BGRA8 surface format");
    const auto caps=physical.getSurfaceCapabilitiesKHR(*surface);
    Require(bool(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst),"Surface cannot be cleared");
    const auto extent=caps.currentExtent;
    unsigned count=caps.minImageCount+1;
    if(caps.maxImageCount) count=std::min(count,caps.maxImageCount);
    vk::SwapchainCreateInfoKHR chain_info{.flags=feedback_enabled?evac_latency::VulkanPresentFeedback::Flags():vk::SwapchainCreateFlagsKHR{},
        .surface=*surface,.minImageCount=count,.imageFormat=chosen->format,.imageColorSpace=chosen->colorSpace,
        .imageExtent=extent,.imageArrayLayers=1,.imageUsage=vk::ImageUsageFlagBits::eTransferDst,
        .preTransform=caps.currentTransform,.compositeAlpha=vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode=mode,.clipped=true};
    vk::UniqueSwapchainKHR bootstrap;
    if(bootstrap_fifo && mode!=vk::PresentModeKHR::eFifo) {
        auto first=chain_info;first.presentMode=vk::PresentModeKHR::eFifo;
        bootstrap=device->createSwapchainKHRUnique(first);
        chain_info.oldSwapchain=*bootstrap;
    }
    auto chain=device->createSwapchainKHRUnique(chain_info);
    bootstrap.reset(); // no image was acquired or presented from this chain
    const auto images=device->getSwapchainImagesKHR(*chain);
    std::vector<bool> initialized(images.size());
    std::vector<vk::UniqueSemaphore> ready;
    for(unsigned i=0;i<images.size();++i) ready.push_back(device->createSemaphoreUnique({}));
    auto acquired=device->createFenceUnique({}),rendered=device->createFenceUnique({});
    auto pool=device->createCommandPoolUnique({.flags=vk::CommandPoolCreateFlagBits::eResetCommandBuffer,.queueFamilyIndex=family});
    auto commands=device->allocateCommandBuffersUnique({.commandPool=*pool,.level=vk::CommandBufferLevel::ePrimary,.commandBufferCount=1});
    const auto command=*commands.front();
    auto queries=device->createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=2});
    std::cout<<"device="<<properties.deviceName.data()<<" mode="<<vk::to_string(mode)<<" extent="<<extent.width<<"x"<<extent.height
             <<" images="<<images.size()<<" seconds="<<seconds<<" feedback="<<feedback_enabled
             <<" own_dc="<<own_dc<<" bootstrap_fifo="<<bootstrap_fifo<<"\n"<<std::flush;
    evac_latency::VulkanPresentFeedback feedback(*device,physical,output,feedback_enabled);
    feedback.Attach(*chain,*surface);
    std::unique_ptr<evac_latency::VulkanPresentWait> wait_observer;
    if(present_wait) wait_observer=std::make_unique<evac_latency::VulkanPresentWait>(
        *device,*chain,std::filesystem::path(output.wstring()+L"-wait.jsonl"));
    WaitClock clock;
    const auto start=evac_latency::FeedbackQpc(),frequency=evac_latency::FeedbackFrequency();
    std::uint64_t frame=0;
    while(!closing && evac_latency::FeedbackQpc()-start<seconds*frequency) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        clock.Until(start+frame*frequency/60);
        const auto began=evac_latency::FeedbackQpc();
        auto index=device->acquireNextImageKHR(*chain,1'000'000'000,{},*acquired).value;
        Require(device->waitForFences(*acquired,true,5'000'000'000)==vk::Result::eSuccess,"Acquire fence timed out");
        device->resetFences(*acquired);
        const auto got_image=evac_latency::FeedbackQpc();
        command.reset();command.begin({.flags=vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command.resetQueryPool(*queries,0,2);
        command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,*queries,0);
        vk::ImageMemoryBarrier barrier{.dstAccessMask=vk::AccessFlagBits::eTransferWrite,
            .oldLayout=initialized[index]?vk::ImageLayout::ePresentSrcKHR:vk::ImageLayout::eUndefined,
            .newLayout=vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=images[index],.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eTransfer,{},nullptr,nullptr,barrier);
        const vk::ClearColorValue color{.float32=std::array<float,4>{.04f,.10f,frame%120<60?.22f:.38f,1}};
        command.clearColorImage(images[index],vk::ImageLayout::eTransferDstOptimal,color,barrier.subresourceRange);
        barrier.srcAccessMask=vk::AccessFlagBits::eTransferWrite;barrier.dstAccessMask={};
        barrier.oldLayout=vk::ImageLayout::eTransferDstOptimal;barrier.newLayout=vk::ImageLayout::ePresentSrcKHR;
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eBottomOfPipe,{},nullptr,nullptr,barrier);
        command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe,*queries,1);command.end();
        const auto semaphore=*ready[index];
        queue.submit(vk::SubmitInfo{.commandBufferCount=1,.pCommandBuffers=&command,.signalSemaphoreCount=1,.pSignalSemaphores=&semaphore},*rendered);
        const auto submitted=evac_latency::FeedbackQpc();
        const auto raw_chain=*chain;
        const auto feedback_extra=feedback.Before();
        const auto extra=wait_observer?wait_observer->Before(feedback_extra):feedback_extra;
        const auto before=evac_latency::FeedbackQpc();
        const auto result=queue.presentKHR({.pNext=extra,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphore,
            .swapchainCount=1,.pSwapchains=&raw_chain,.pImageIndices=&index});
        const auto after=evac_latency::FeedbackQpc();
        feedback.After(frame,before,after,mode,result);
        if(wait_observer) wait_observer->After(frame,before,after,result);
        Require(device->waitForFences(*rendered,true,5'000'000'000)==vk::Result::eSuccess,"GPU fence timed out");
        device->resetFences(*rendered);
        std::array<std::uint64_t,2> timestamps{};
        Require(device->getQueryPoolResults(*queries,0,2,sizeof(timestamps),timestamps.data(),sizeof(std::uint64_t),
            vk::QueryResultFlagBits::e64)==vk::Result::eSuccess,"GPU timestamps unavailable");
        feedback.Sample(frame,began,got_image,submitted,evac_latency::FeedbackQpc(),
            (timestamps[1]-timestamps[0])*properties.limits.timestampPeriod/1000.0);
        initialized[index]=true;++frame;
    }
    device->waitIdle();
    if(wait_observer) wait_observer->Stop();
    // Feedback may become available after GPU completion; bounded drain only.
    for(unsigned i=0;i<30;++i) {feedback.Drain();Sleep(10);}
    std::cout<<"frames="<<frame<<" feedback="<<output<<"\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"Vulkan present bench failed: "<<error.what()<<"\n";return 1;
}
