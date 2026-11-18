#include <Windows.h>
#include "thread_id.hpp"
#include "wsi.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
class Window final : public Vulkan::WSIPlatform {
public:
    HWND handle = CreateWindowExW(0, L"STATIC", L"PS2 WSI contract", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() override { if (handle) DestroyWindow(handle); }
    VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override {
        VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        info.hinstance = GetModuleHandleW(nullptr);
        info.hwnd = handle;
        VkSurfaceKHR surface{};
        return vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) == VK_SUCCESS ? surface : VK_NULL_HANDLE;
    }
    std::vector<const char*> get_instance_extensions() override {
        return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    }
    uintptr_t get_fullscreen_monitor() override {
        return reinterpret_cast<uintptr_t>(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST));
    }
    uint32_t get_surface_width() override { return 640; }
    uint32_t get_surface_height() override { return 480; }
    bool alive(Vulkan::WSI&) override { return true; }
    void poll_input() override {}
    void poll_input_async(Granite::InputTrackerHandler*) override {}
};

template<class T>
const T* Find(const void* chain, VkStructureType type) {
    auto* node = static_cast<const VkBaseInStructure*>(chain);
    while (node && node->sType != type) node = node->pNext;
    return reinterpret_cast<const T*>(node);
}

const std::array modes{VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
HMONITOR monitor{};
unsigned createCalls{}, formatCalls{}, alternateQueries{}, acquireCalls{};
bool valid = true, primaryBounded = false;
void Check(bool match, const char* message) {
    if (!match) { std::cerr << message << '\n'; valid = false; }
}

void CheckWindow(const void* chain) {
    auto* policy = Find<VkSurfaceFullScreenExclusiveInfoEXT>(chain, VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT);
    auto* target = Find<VkSurfaceFullScreenExclusiveWin32InfoEXT>(chain, VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT);
    Check(policy && policy->fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT,
          "A query or create lost the fullscreen policy");
    Check(target && target->hmonitor == monitor, "A query or create lost the monitor");
}

VKAPI_ATTR VkResult VKAPI_CALL PresentModes(VkPhysicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* query,
                                            uint32_t* count, VkPresentModeKHR* output) {
    CheckWindow(query->pNext);
    if (output && *count < modes.size()) return VK_INCOMPLETE;
    if (output) std::copy(modes.begin(), modes.end(), output);
    *count = uint32_t(modes.size());
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL Capabilities(VkPhysicalDevice gpu, const VkPhysicalDeviceSurfaceInfo2KHR* query,
                                            VkSurfaceCapabilities2KHR* out) {
    auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, query->surface, &out->surfaceCapabilities);
    if (result != VK_SUCCESS) return result;
    auto* exclusive = const_cast<VkSurfaceCapabilitiesFullScreenExclusiveEXT*>(
        Find<VkSurfaceCapabilitiesFullScreenExclusiveEXT>(out->pNext, VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT));
    if (exclusive) {
        exclusive->fullScreenExclusiveSupported = VK_TRUE;
        return VK_SUCCESS;
    }
    CheckWindow(query->pNext);
    auto* mode = Find<VkSurfacePresentModeKHR>(query->pNext, VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR);
    auto& caps = out->surfaceCapabilities;
    caps.minImageCount = 2;
    caps.maxImageCount = 8;
    if (!mode) {
        auto* wait = const_cast<VkSurfaceCapabilitiesPresentWait2KHR*>(
            Find<VkSurfaceCapabilitiesPresentWait2KHR>(out->pNext, VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR));
        if (wait) wait->presentWait2Supported = VK_TRUE;
        return VK_SUCCESS;
    }
    if (mode->presentMode == VK_PRESENT_MODE_FIFO_KHR) {
        caps.maxImageCount = primaryBounded ? 3 : 0;
        auto* group = const_cast<VkSurfacePresentModeCompatibilityKHR*>(
            Find<VkSurfacePresentModeCompatibilityKHR>(out->pNext, VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR));
        if (!group || !group->pPresentModes || group->presentModeCount < modes.size()) return VK_ERROR_UNKNOWN;
        std::copy(modes.begin(), modes.end(), group->pPresentModes);
        group->presentModeCount = uint32_t(modes.size());
    } else {
        ++alternateQueries;
        if (mode->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) caps.maxImageCount = primaryBounded ? 0 : 3;
        else { caps.minImageCount = 4; caps.maxImageCount = 4; }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL Formats(VkPhysicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* query,
                                       uint32_t* count, VkSurfaceFormat2KHR* output) {
    ++formatCalls;
    CheckWindow(query->pNext);
    auto* mode = Find<VkSurfacePresentModeKHR>(query->pNext, VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR);
    Check(mode && mode->presentMode == VK_PRESENT_MODE_FIFO_KHR, "Formats queried with the wrong initial mode");
    if (output && *count) output->surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL RejectCreate(VkDevice, const VkSwapchainCreateInfoKHR* info,
                                            const VkAllocationCallbacks*, VkSwapchainKHR* handle) {
    ++createCalls;
    CheckWindow(info->pNext);
    Check(info->presentMode == VK_PRESENT_MODE_FIFO_KHR && info->minImageCount == 3,
          "The create does not fit the selected modes' common image range");
    auto* group = Find<VkSwapchainPresentModesCreateInfoKHR>(info->pNext, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR);
    Check(group && group->presentModeCount == 2 && group->pPresentModes[0] == VK_PRESENT_MODE_IMMEDIATE_KHR &&
          group->pPresentModes[1] == VK_PRESENT_MODE_FIFO_KHR, "Create advertised a mode with a disjoint image range");
    *handle = VK_NULL_HANDLE;
    return VK_ERROR_INITIALIZATION_FAILED;
}
VKAPI_ATTR VkResult VKAPI_CALL Acquire(VkDevice, VkSwapchainKHR) {
    ++acquireCalls;
    return VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
}
}

int main(int argc, char** argv) {
    primaryBounded = argc == 2 && std::string_view(argv[1]) == "primary-bounded";
    Window window;
    if (!window.handle || !Vulkan::Context::init_loader(nullptr)) return 1;
    Util::register_thread_index(0);
    Vulkan::WSI wsi;
    wsi.set_platform(&window);
    wsi.set_backbuffer_format(Vulkan::BackbufferFormat::UNORM);
    wsi.set_present_mode(Vulkan::PresentMode::SyncToVBlank);
    wsi.set_present_low_latency_mode(false);
    if (!wsi.init_context_from_platform(1, {}) || !wsi.init_device()) return 1;
    auto& device = wsi.get_device();
    const auto& ext = device.get_device_features();
    if (!ext.supports_full_screen_exclusive || !ext.supports_surface_capabilities2 ||
        !ext.swapchain_maintenance1_features.swapchainMaintenance1 || ext.driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS)
        return 77; // The exclusive capability branch is unavailable on this device.
    monitor = reinterpret_cast<HMONITOR>(window.get_fullscreen_monitor());
    _putenv_s("GRANITE_EXCLUSIVE_FULL_SCREEN", "1");
    _putenv_s("GRANITE_VULKAN_SWAPCHAIN_IMAGES", "7");
    // Only this process's dispatch is replaced, after Granite created a real device.
    vkGetPhysicalDeviceSurfacePresentModes2EXT = PresentModes;
    vkGetPhysicalDeviceSurfaceCapabilities2KHR = Capabilities;
    vkGetPhysicalDeviceSurfaceFormats2KHR = Formats;
    const_cast<VolkDeviceTable&>(device.get_device_table()).vkCreateSwapchainKHR = RejectCreate;
    vkAcquireFullScreenExclusiveModeEXT = Acquire;
    Check(!wsi.init_surface_swapchain(), "Expected the rejected swapchain to fail");
    Check(createCalls && alternateQueries == 2 * createCalls && formatCalls == 2 * createCalls,
          "The mode and format queries did not run on every creation attempt");
    Check(acquireCalls == 0, "Acquired exclusive mode after swapchain creation failed");
    std::cout << "create_attempts=" << createCalls << " alternate_queries=" << alternateQueries
              << " format_queries=" << formatCalls << " acquire_after_failure=" << acquireCalls << '\n';
    return valid ? 0 : 1;
}
