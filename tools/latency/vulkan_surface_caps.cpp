// Diagnostic only: query the timing stages exposed by an actual Win32 surface.
// No game, input injection, display-mode change, or GPU work is performed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

static void Check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "%s returned %d\n", operation, static_cast<int>(result));
    throw std::runtime_error(operation);
  }
}

int main() {
  HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
  if (!loader) return 2;
  auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
  if (!get) return 3;
  const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
  VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  application.pApplicationName = "EVAC latency surface capability query";
  application.apiVersion = VK_API_VERSION_1_1;
  const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME};
  VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create.pApplicationInfo = &application;
  create.enabledExtensionCount = 3;
  create.ppEnabledExtensionNames = extensions;
  VkInstance instance = nullptr;
  Check(create_instance(&create, nullptr, &instance), "CreateInstance");
#define LOAD(name) const auto name = reinterpret_cast<PFN_##name>(get(instance, #name)); if (!name) return 4
  LOAD(vkDestroyInstance);
  LOAD(vkCreateWin32SurfaceKHR);
  LOAD(vkDestroySurfaceKHR);
  LOAD(vkEnumeratePhysicalDevices);
  LOAD(vkGetPhysicalDeviceProperties);
  LOAD(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
  LOAD(vkEnumerateDeviceExtensionProperties);
#undef LOAD
  const auto module = GetModuleHandleW(nullptr);
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = module;
  window_class.lpszClassName = L"EVACLatencySurfaceProbe";
  if (!RegisterClassW(&window_class)) return 5;
  HWND window = CreateWindowExW(0, window_class.lpszClassName,
      L"EVAC latency surface capability query", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, module, nullptr);
  if (!window) return 6;
  ShowWindow(window, SW_SHOWNOACTIVATE);
  VkWin32SurfaceCreateInfoKHR surface_create{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  surface_create.hinstance = module;
  surface_create.hwnd = window;
  VkSurfaceKHR surface = nullptr;
  Check(vkCreateWin32SurfaceKHR(instance, &surface_create, nullptr, &surface), "CreateSurface");
  unsigned count = 0;
  Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "CountDevices");
  std::vector<VkPhysicalDevice> devices(count);
  Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Devices");
  for (auto device : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    unsigned extension_count = 0;
    Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr), "CountExtensions");
    std::vector<VkExtensionProperties> available(extension_count);
    Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available.data()), "Extensions");
    unsigned timing_revision = 0;
    for (const auto& extension : available)
      if (std::string_view(extension.extensionName) == VK_EXT_PRESENT_TIMING_EXTENSION_NAME)
        timing_revision = extension.specVersion;
    if (!timing_revision) {
      std::printf("device=%s present_timing_extension=unsupported\n", properties.deviceName);
      continue;
    }
    VkPresentTimingSurfaceCapabilitiesEXT timing{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
    VkSurfaceCapabilities2KHR capabilities{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
    capabilities.pNext = &timing;
    VkPhysicalDeviceSurfaceInfo2KHR info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
    info.surface = surface;
    Check(vkGetPhysicalDeviceSurfaceCapabilities2KHR(device, &info, &capabilities), "SurfaceCapabilities2");
    std::printf("device=%s revision=%u surface_supported=%u absolute=%u relative=%u stages=0x%X extent=%ux%u\n",
      properties.deviceName, timing_revision, timing.presentTimingSupported,
      timing.presentAtAbsoluteTimeSupported, timing.presentAtRelativeTimeSupported,
      timing.presentStageQueries, capabilities.surfaceCapabilities.currentExtent.width,
      capabilities.surfaceCapabilities.currentExtent.height);
    for (unsigned bit = 1; bit <= 8; bit <<= 1)
      std::printf("stage=%u supported=%u\n", bit, !!(timing.presentStageQueries & bit));
  }
  vkDestroySurfaceKHR(instance, surface, nullptr);
  DestroyWindow(window);
  UnregisterClassW(window_class.lpszClassName, module);
  vkDestroyInstance(instance, nullptr);
  FreeLibrary(loader);
  return 0;
}
