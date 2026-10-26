#include "vulkan_device.h"
#include "present_mode.h"
#include "presentation_extensions.h"

#include <windows.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <list>
#include <bit>
#include <chrono>
#include <new>

namespace kt::gfx {
namespace {
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) { throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result)); }
}
template <typename Function>
Function Require(PFN_vkVoidFunction pointer, const char* name) {
    if (!pointer) { throw std::runtime_error(std::string("Missing Vulkan function: ") + name); }
    return reinterpret_cast<Function>(pointer);
}
VkFormat Format(ColorFormat format) { return format == ColorFormat::Bgra8Srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_R8G8B8A8_SRGB; }
template <typename Item, typename Function>
std::vector<Item> Enumerate(const char* operation, std::uint32_t maximum, Function function) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        std::uint32_t count{};
        Check(function(&count, static_cast<Item*>(nullptr)), operation);
        if (count > maximum) { throw std::runtime_error(std::string(operation) + " returned an excessive count"); }
        if (!count) { return {}; }
        std::vector<Item> items(count);
        const auto result = function(&count, items.data());
        if (result == VK_INCOMPLETE) { continue; }
        Check(result, operation);
        if (count > items.size()) { throw std::runtime_error(std::string(operation) + " exceeded supplied capacity"); }
        items.resize(count);
        return items;
    }
    throw std::runtime_error(std::string(operation) + " kept changing during enumeration");
}
std::string LogName(const char* input) {
    std::string result;
    for (std::size_t i = 0; i < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE && input[i]; ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c == '\\' || c == '"') { result += '\\'; }
        result += c >= 32 && c < 127 ? static_cast<char>(c) : '?';
    }
    return result;
}

std::vector<std::uint32_t> PipelineKey(const RasterDraw& draw, std::span<const VkDescriptorSetLayoutBinding> bindings) {
    // Serialize values, never C++/Vulkan struct padding or resource addresses.
    // All other pipeline/render-pass/layout fields are fixed below in Draw.
    std::vector<std::uint32_t> key;
    const auto words = [&](std::span<const std::uint32_t> program) {
        if (program.size() > UINT32_MAX) { throw std::runtime_error("Oversized pipeline program"); }
        key.push_back(static_cast<std::uint32_t>(program.size())); key.insert(key.end(), program.begin(), program.end());
    };
    words(draw.vertex); words(draw.pixel); words(draw.rectangle_control); words(draw.rectangle_evaluation);
    key.insert(key.end(), {draw.width, draw.height, draw.rectangle, draw.quad, draw.srgb,
        draw.cull_front, draw.cull_back, draw.clockwise, draw.depth, draw.stencil, draw.clear_depth,
        draw.clear_stencil, draw.depth_write, draw.depth_compare, draw.color_mask, draw.blend.enabled,
        static_cast<unsigned>(draw.blend.source_color), static_cast<unsigned>(draw.blend.destination_color),
        static_cast<unsigned>(draw.blend.source_alpha), static_cast<unsigned>(draw.blend.destination_alpha),
        static_cast<unsigned>(draw.blend.color), static_cast<unsigned>(draw.blend.alpha)});
    for (const auto value : draw.viewport) { key.push_back(std::bit_cast<std::uint32_t>(value)); }
    for (const auto value : draw.scissor) { key.push_back(std::bit_cast<std::uint32_t>(value)); }
    for (const auto value : draw.blend.constants) { key.push_back(std::bit_cast<std::uint32_t>(value)); }
    for (const auto& stencil : {draw.stencil_front, draw.stencil_back}) {
        key.insert(key.end(), {stencil.fail, stencil.pass, stencil.depth_fail, stencil.compare,
                              stencil.reference, stencil.compare_mask, stencil.write_mask});
    }
    key.push_back(static_cast<unsigned>(bindings.size()));
    for (const auto& binding : bindings) { key.insert(key.end(), {binding.binding, static_cast<unsigned>(binding.descriptorType), binding.descriptorCount, binding.stageFlags}); }
    return key;
}
}

std::uint32_t SelectMemoryType(std::span<const std::uint32_t> flags, std::uint32_t bits,
    std::uint32_t required, std::uint32_t preferred) noexcept {
    if (flags.size() > 32) { return UINT32_MAX; }
    for (const auto wanted : {required | preferred, required}) {
        for (std::uint32_t index = 0; index < flags.size(); ++index) {
            if ((bits & (1u << index)) && (flags[index] & wanted) == wanted) { return index; }
        }
    }
    return UINT32_MAX;
}

struct VulkanDevice::Impl {
    HMODULE loader{};
    PFN_vkGetInstanceProcAddr get_instance{};
    PFN_vkGetDeviceProcAddr get_device{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkPipelineCache pipeline_cache{};
    VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties properties{};
    std::uint32_t queue_family{};
    bool surface_maintenance{}, present_fences{};
    bool vsync = true;
    bool validation{};
    std::atomic<unsigned> validation_errors{};
    VkDebugUtilsMessengerEXT debug_messenger{};
    VkSwapchainKHR swapchain{};
    VkExtent2D swapchain_extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<bool> swapchain_initialized;
    PFN_vkDestroyInstance destroy_instance{};
    PFN_vkDestroySurfaceKHR destroy_surface{};
    PFN_vkDestroyDevice destroy_device{};
    PFN_vkDeviceWaitIdle wait_idle{};
    PFN_vkCreateImage create_image{};
    PFN_vkDestroyImage destroy_image{};
    PFN_vkGetImageMemoryRequirements image_requirements{};
    PFN_vkAllocateMemory allocate_memory{};
    PFN_vkFreeMemory free_memory{};
    PFN_vkBindImageMemory bind_image{};
    PFN_vkCreateImageView create_view{};
    PFN_vkDestroyImageView destroy_view{};
    struct Image { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; DisplayLayout layout{}; bool initialized{}; };
    std::map<std::uint64_t, Image> images;
    struct CachedTexture {
        unsigned width{}, height{}; bool srgb{}, initialized{};
        std::array<std::uint32_t, 4> components{};
        std::vector<std::uint8_t> pixels;
        std::shared_ptr<const std::vector<std::uint8_t>> snapshot;
        VkImage image{}; VkImageView view{}; std::uint64_t bytes{};
        // Callbacks capture the owning device explicitly, never Draw locals.
        std::vector<std::function<void()>> cleanup;
        ~CachedTexture() { for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it) { (*it)(); } }
    };
    std::list<std::shared_ptr<CachedTexture>> texture_cache;
    struct CachedPipeline {
        Impl* owner{};
        std::vector<std::uint32_t> key;
        VkDescriptorSetLayout descriptors{};
        VkPipelineLayout layout{};
        VkRenderPass render_pass{};
        VkPipeline pipeline{};
        ~CachedPipeline() {
            if (pipeline) { owner->Device<PFN_vkDestroyPipeline>("vkDestroyPipeline")(owner->device, pipeline, nullptr); }
            if (render_pass) { owner->Device<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(owner->device, render_pass, nullptr); }
            if (layout) { owner->Device<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(owner->device, layout, nullptr); }
            if (descriptors) { owner->Device<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(owner->device, descriptors, nullptr); }
        }
    };
    std::list<std::shared_ptr<CachedPipeline>> pipelines;
    struct CachedBuffer {
        Impl* owner{};
        VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{};
        VkDeviceSize size{}, allocation_bytes{};
        VkBufferUsageFlags usage{}; VkMemoryPropertyFlags properties{};
        ~CachedBuffer() {
            if (mapped) { owner->Device<PFN_vkUnmapMemory>("vkUnmapMemory")(owner->device, memory); }
            if (handle) { owner->Device<PFN_vkDestroyBuffer>("vkDestroyBuffer")(owner->device, handle, nullptr); }
            if (memory) { owner->free_memory(owner->device, memory, nullptr); }
        }
    };
    std::list<std::shared_ptr<CachedBuffer>> buffers;
    struct CachedAttachment {
        unsigned width{}, height{}; VkFormat format{}; VkImageUsageFlags usage{}; VkImageAspectFlags aspect{};
        VkImage image{}; VkImageView view{}; std::uint64_t bytes{};
        bool initialized{};
        std::vector<std::uint8_t> color, stencil;
        std::vector<float> depth;
        std::shared_ptr<CachedBuffer> color_readback, depth_readback, stencil_readback;
        std::uint64_t color_bytes{}, depth_bytes{}, stencil_bytes{};
        std::uint64_t SnapshotBytes() const {
            return color.capacity() + stencil.capacity() + depth.capacity() * sizeof(float) +
                (color_readback ? color_readback->allocation_bytes : 0) + (depth_readback ? depth_readback->allocation_bytes : 0) +
                (stencil_readback ? stencil_readback->allocation_bytes : 0);
        }
        void ReleaseReadbacks() { color_readback.reset(); depth_readback.reset(); stencil_readback.reset(); color_bytes = depth_bytes = stencil_bytes = 0; }
        std::vector<std::function<void()>> cleanup;
        ~CachedAttachment() { for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it) { (*it)(); } }
    };
    std::list<std::shared_ptr<CachedAttachment>> attachments;
    RasterCacheStats cache_stats;
    void TrimAttachments() {
        cache_stats.attachment_bytes = cache_stats.attachment_snapshot_bytes = 0;
        for (const auto& value : attachments) {
            cache_stats.attachment_bytes += value->bytes; cache_stats.attachment_snapshot_bytes += value->SnapshotBytes();
        }
        while (!attachments.empty() && (attachments.size() > 8 ||
            cache_stats.attachment_bytes + cache_stats.attachment_snapshot_bytes > 128ull * 1024 * 1024)) {
            cache_stats.attachment_bytes -= attachments.back()->bytes;
            cache_stats.attachment_snapshot_bytes -= attachments.back()->SnapshotBytes();
            attachments.pop_back(); ++cache_stats.attachment_evictions;
        }
    }
    std::uint64_t next = 1;

    template <typename Function> Function Instance(const char* name) { return Require<Function>(get_instance(instance, name), name); }
    template <typename Function> Function Device(const char* name) { return Require<Function>(get_device(device, name), name); }
    void Erase(Image& image) noexcept {
        if (image.view) { destroy_view(device, image.view, nullptr); }
        if (image.image) { destroy_image(device, image.image, nullptr); }
        if (image.memory) { free_memory(device, image.memory, nullptr); }
        image = {};
    }
    ~Impl() {
        if (device && wait_idle) {
            const auto result = wait_idle(device);
            if (result != VK_SUCCESS) { std::fprintf(stderr, "Vulkan shutdown wait: %d\n", result); }
        }
        for (auto& [id, image] : images) { (void)id; Erase(image); }
        texture_cache.clear();
        pipelines.clear();
        buffers.clear();
        attachments.clear();
        if (swapchain) { Device<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device, swapchain, nullptr); }
        if (pipeline_cache) { Device<PFN_vkDestroyPipelineCache>("vkDestroyPipelineCache")(device, pipeline_cache, nullptr); }
        if (device && destroy_device) { destroy_device(device, nullptr); }
        if (surface && destroy_surface) { destroy_surface(instance, surface, nullptr); }
        if (debug_messenger) { Instance<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT")(instance, debug_messenger, nullptr); }
        if (instance && destroy_instance) { destroy_instance(instance, nullptr); }
        if (loader) { FreeLibrary(loader); }
    }

    void Initialize(void* native_window) {
        const auto window = static_cast<HWND>(native_window);
        if (!window || !IsWindow(window)) { throw std::runtime_error("A live owned window is required for Vulkan"); }
        loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loader) { throw std::runtime_error("The system Vulkan loader is not installed"); }
        get_instance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
        if (!get_instance) { throw std::runtime_error("Invalid system Vulkan loader"); }
        const auto create_instance = Instance<PFN_vkCreateInstance>("vkCreateInstance");
        const auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(get_instance(nullptr, "vkEnumerateInstanceVersion"));
        std::uint32_t version = VK_API_VERSION_1_0;
        if (enumerate_version) { Check(enumerate_version(&version), "vkEnumerateInstanceVersion"); }
        if (version < VK_API_VERSION_1_2) { throw std::runtime_error("Vulkan 1.2 or newer is required"); }
        std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        const auto enumerate_instance_extensions = Instance<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
        const auto instance_extensions = Enumerate<VkExtensionProperties>("vkEnumerateInstanceExtensionProperties", 16384,
            [&](auto* count, auto* data) { return enumerate_instance_extensions(nullptr, count, data); });
        const auto has_instance_extension = [&](const char* name) {
            return std::any_of(instance_extensions.begin(), instance_extensions.end(), [&](const auto& entry) { return std::strcmp(entry.extensionName, name) == 0; });
        };
        wchar_t requested_validation[8]{};
        validation = GetEnvironmentVariableW(L"KETSUI_VULKAN_VALIDATE", requested_validation, 8) && requested_validation[0] == L'1';
        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        if (validation) {
            const auto enumerate_layers = Instance<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
            const auto layers = Enumerate<VkLayerProperties>("vkEnumerateInstanceLayerProperties", 1024,
                [&](auto* count, auto* data) { return enumerate_layers(count, data); });
            if (!std::any_of(layers.begin(), layers.end(), [&](const auto& layer) { return std::strcmp(layer.layerName, validation_layer) == 0; }) ||
                !has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) { throw std::runtime_error("Requested Khronos Vulkan validation layer is unavailable"); }
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            const auto layer_extensions = Enumerate<VkExtensionProperties>("vkEnumerateInstanceExtensionProperties(validation)", 1024,
                [&](auto* count, auto* data) { return enumerate_instance_extensions(validation_layer, count, data); });
            if (!std::any_of(layer_extensions.begin(), layer_extensions.end(), [](const auto& entry) {
                return std::strcmp(entry.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
            })) { throw std::runtime_error("Requested validation layer lacks synchronization validation features"); }
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        const auto maintenance_surfaces = cave::ps4::presentation::DiagnosticCompatibility()
            ? cave::ps4::presentation::Surfaces{} : cave::ps4::presentation::SurfaceExtensions(has_instance_extension);
        surface_maintenance = maintenance_surfaces.Supported();
        if (surface_maintenance) {
            extensions.push_back(cave::ps4::presentation::kCapabilities);
            if (maintenance_surfaces.ext) extensions.push_back(cave::ps4::presentation::kSurfaceExt);
            if (maintenance_surfaces.khr) extensions.push_back(cave::ps4::presentation::kSurfaceKhr);
        }
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Ketsui PC independent graphics";
        app.applicationVersion = 1;
        app.pEngineName = "KetsuiGraphics";
        app.engineVersion = 1;
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app;
        instance_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        instance_info.ppEnabledExtensionNames = extensions.data();
        const VkValidationFeatureEnableEXT sync_validation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation_features{}; validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validation_features.enabledValidationFeatureCount = 1; validation_features.pEnabledValidationFeatures = &sync_validation;
        if (validation) {
            instance_info.enabledLayerCount = 1; instance_info.ppEnabledLayerNames = &validation_layer;
            instance_info.pNext = &validation_features;
        }
        Check(create_instance(&instance_info, nullptr, &instance), "vkCreateInstance");
        destroy_instance = Instance<PFN_vkDestroyInstance>("vkDestroyInstance");
        if (validation) {
            VkDebugUtilsMessengerCreateInfoEXT debug{}; debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pUserData = this;
            debug.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                const VkDebugUtilsMessengerCallbackDataEXT* message, void* raw) -> VkBool32 {
                auto& self = *static_cast<Impl*>(raw);
                if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { ++self.validation_errors; }
                std::fprintf(stderr, "[Vulkan validation %s] %s\n", severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning", message->pMessage);
                return VK_FALSE;
            };
            Check(Instance<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT")(instance, &debug, nullptr, &debug_messenger), "vkCreateDebugUtilsMessengerEXT");
        }
        destroy_surface = Instance<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
        const auto create_surface = Instance<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR");
        VkWin32SurfaceCreateInfoKHR surface_info{};
        surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_info.hinstance = GetModuleHandleW(nullptr);
        surface_info.hwnd = window;
        Check(create_surface(instance, &surface_info, nullptr, &surface), "vkCreateWin32SurfaceKHR");

        const auto enumerate_devices = Instance<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        const auto get_properties = Instance<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
        const auto get_families = Instance<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        const auto surface_support = Instance<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR");
        const auto enumerate_extensions = Instance<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        const auto format_properties = Instance<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
        const auto devices = Enumerate<VkPhysicalDevice>("vkEnumeratePhysicalDevices", 256,
            [&](auto* count, auto* data) { return enumerate_devices(instance, count, data); });
        if (devices.empty()) { throw std::runtime_error("No usable Vulkan physical device"); }
        int best_score = -1;
        for (const auto candidate : devices) {
            VkPhysicalDeviceProperties candidate_properties{};
            get_properties(candidate, &candidate_properties);
            std::fprintf(stderr, "Vulkan GPU candidate: %s vendor=0x%x device=0x%x api=0x%x driver=0x%x\n",
                candidate_properties.deviceName, candidate_properties.vendorID, candidate_properties.deviceID,
                candidate_properties.apiVersion, candidate_properties.driverVersion);
            if (candidate_properties.apiVersion < VK_API_VERSION_1_2) { continue; }
            VkPhysicalDeviceVulkan12Features f12{}; f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote{}; demote.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT; f12.pNext = &demote;
            VkPhysicalDeviceVulkan11Features f11{}; f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES; f11.pNext = &f12;
            VkPhysicalDeviceFeatures2 features{}; features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; features.pNext = &f11;
            Instance<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(candidate, &features);
            if (!features.features.shaderInt16 || !features.features.shaderInt64 || !features.features.tessellationShader ||
                !features.features.shaderClipDistance || !features.features.shaderCullDistance ||
                !f11.storageBuffer16BitAccess || !f11.shaderDrawParameters || !f12.storageBuffer8BitAccess || !f12.shaderInt8 || !demote.shaderDemoteToHelperInvocation) { continue; }
            const auto device_extensions = Enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties", 16384,
                [&](auto* count, auto* data) { return enumerate_extensions(candidate, nullptr, count, data); });
            bool has_swapchain = false, has_demote = false;
            for (const auto& extension : device_extensions) {
                has_swapchain |= std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                has_demote |= std::strcmp(extension.extensionName, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME) == 0;
            }
            if (!has_swapchain || !has_demote) { continue; }
            bool formats = true;
            for (const auto format : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
                VkFormatProperties capabilities{};
                format_properties(candidate, format, &capabilities);
                constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
                formats &= (capabilities.optimalTilingFeatures & required) == required;
            }
            if (!formats) { continue; }
            std::uint32_t family_count{};
            get_families(candidate, &family_count, nullptr);
            if (!family_count || family_count > 256) { continue; }
            std::vector<VkQueueFamilyProperties> families(family_count);
            get_families(candidate, &family_count, families.data());
            for (std::uint32_t family = 0; family < family_count; ++family) {
                VkBool32 supported{};
                Check(surface_support(candidate, family, surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (!families[family].queueCount || !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !supported) { continue; }
                const int score = candidate_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 :
                    candidate_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 50 : 0;
                if (score > best_score) { best_score = score; physical = candidate; properties = candidate_properties; queue_family = family; }
            }
        }
        if (!physical) { throw std::runtime_error("No graphics/present queue with required SDR image formats"); }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        const auto selected_extensions = Enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties", 16384,
            [&](auto* count, auto* data) { return enumerate_extensions(physical, nullptr, count, data); });
        const auto maintenance_name = cave::ps4::presentation::SwapchainExtension(maintenance_surfaces, [&](const char* name) {
            return std::any_of(selected_extensions.begin(), selected_extensions.end(),
                [&](const auto& entry) { return std::strcmp(entry.extensionName, name) == 0; });
        });
        present_fences = maintenance_name != nullptr;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{}; maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        if (present_fences) {
            VkPhysicalDeviceFeatures2 queried{}; queried.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; queried.pNext = &maintenance;
            Instance<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(physical, &queried);
            present_fences = maintenance.swapchainMaintenance1 != 0;
        }
        std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME};
        if (present_fences) { device_extensions.push_back(maintenance_name); }
        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = static_cast<unsigned>(device_extensions.size());
        device_info.ppEnabledExtensionNames = device_extensions.data();
        VkPhysicalDeviceVulkan12Features f12{}; f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        f12.storageBuffer8BitAccess = f12.shaderInt8 = VK_TRUE;
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote{}; demote.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
        demote.shaderDemoteToHelperInvocation = VK_TRUE; demote.pNext = present_fences ? &maintenance : nullptr; f12.pNext = &demote;
        VkPhysicalDeviceVulkan11Features f11{}; f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES; f11.pNext = &f12;
        f11.storageBuffer16BitAccess = f11.shaderDrawParameters = VK_TRUE;
        VkPhysicalDeviceFeatures features{};
        features.shaderInt16 = features.shaderInt64 = features.tessellationShader = features.shaderClipDistance = features.shaderCullDistance = VK_TRUE;
        device_info.pNext = &f11; device_info.pEnabledFeatures = &features;
        const auto create_device = Instance<PFN_vkCreateDevice>("vkCreateDevice");
        // Keep a valid destruction trampoline even if loading a later device
        // dispatch entry fails after creation.
        get_device = Instance<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        destroy_device = Instance<PFN_vkDestroyDevice>("vkDestroyDevice");
        Check(create_device(physical, &device_info, nullptr, &device), "vkCreateDevice");
        destroy_device = Device<PFN_vkDestroyDevice>("vkDestroyDevice");
        wait_idle = Device<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
        VkPipelineCacheCreateInfo pipeline_cache_info{}; pipeline_cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        Check(Device<PFN_vkCreatePipelineCache>("vkCreatePipelineCache")(device, &pipeline_cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache");
        create_image = Device<PFN_vkCreateImage>("vkCreateImage");
        destroy_image = Device<PFN_vkDestroyImage>("vkDestroyImage");
        image_requirements = Device<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements");
        allocate_memory = Device<PFN_vkAllocateMemory>("vkAllocateMemory");
        free_memory = Device<PFN_vkFreeMemory>("vkFreeMemory");
        bind_image = Device<PFN_vkBindImageMemory>("vkBindImageMemory");
        create_view = Device<PFN_vkCreateImageView>("vkCreateImageView");
        destroy_view = Device<PFN_vkDestroyImageView>("vkDestroyImageView");
        Device<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, queue_family, 0, &queue);
        Instance<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical, &memory);
        const auto name = LogName(properties.deviceName);
        std::printf("{\"event\":\"native_graphics_device\",\"backend\":\"Vulkan\",\"device\":\"%s\",\"vendor\":%u,\"device_id\":%u,\"api_version\":%u,\"driver_version\":%u,\"queue_family\":%u,\"game_commands\":false}\n",
                    name.c_str(), properties.vendorID, properties.deviceID, properties.apiVersion, properties.driverVersion, queue_family);
        std::printf("{\"event\":\"native_graphics_features\",\"spirv\":105,\"storage8\":true,\"storage16\":true,\"int8\":true,\"int16\":true,\"int64\":true,\"rectangle_tessellation\":true,\"present_fences\":%s}\n", present_fences ? "true" : "false");
        std::printf("{\"event\":\"native_graphics_validation\",\"enabled\":%s,\"synchronization\":%s}\n", validation ? "true" : "false", validation ? "true" : "false");
    }
};

VulkanDevice::VulkanDevice(void* native_window, bool vsync) : impl_(std::make_unique<Impl>()) {
    impl_->vsync = vsync;
    impl_->Initialize(native_window);
}
VulkanDevice::~VulkanDevice() = default;
std::size_t VulkanDevice::ImageCount() const { return impl_->images.size(); }
void VulkanDevice::WaitIdle() { Check(impl_->wait_idle(impl_->device), "vkDeviceWaitIdle"); }

std::uint64_t VulkanDevice::CreateDisplayImage(const DisplayLayout& layout) {
    if (!layout.width || !layout.height || layout.width > impl_->properties.limits.maxImageDimension2D || layout.height > impl_->properties.limits.maxImageDimension2D) {
        throw std::runtime_error("Display image dimensions exceed device limits");
    }
    const auto id = impl_->next++;
    auto [iterator, inserted] = impl_->images.try_emplace(id);
    if (!inserted) { throw std::runtime_error("Vulkan image identifier exhausted"); }
    auto& image = iterator->second;
    image.layout = layout;
    try {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = Format(layout.format);
        info.extent = {layout.width, layout.height, 1};
        info.mipLevels = 1; info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(impl_->create_image(impl_->device, &info, nullptr, &image.image), "vkCreateImage");
        VkMemoryRequirements requirements{};
        impl_->image_requirements(impl_->device, image.image, &requirements);
        std::uint32_t memory_type = UINT32_MAX;
        for (std::uint32_t index = 0; index < impl_->memory.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) && (impl_->memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memory_type = index; break;
            }
        }
        if (memory_type == UINT32_MAX) { throw std::runtime_error("No compatible device-local image memory"); }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        Check(impl_->allocate_memory(impl_->device, &allocation, nullptr, &image.memory), "vkAllocateMemory");
        Check(impl_->bind_image(impl_->device, image.image, image.memory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = info.format;
        view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Check(impl_->create_view(impl_->device, &view, nullptr, &image.view), "vkCreateImageView");
        std::printf("{\"event\":\"native_graphics_image\",\"id\":%llu,\"width\":%u,\"height\":%u,\"guest_bytes\":%llu,\"device_bytes\":%llu,\"initialized\":false}\n",
                    id, layout.width, layout.height, layout.bytes, requirements.size);
        return id;
    } catch (...) { impl_->Erase(image); impl_->images.erase(iterator); throw; }
}

void VulkanDevice::DestroyDisplayImage(std::uint64_t id) {
    const auto iterator = impl_->images.find(id);
    if (iterator == impl_->images.end()) { throw std::runtime_error("Invalid Vulkan display image"); }
    // Caller drains before releasing images which have been submitted. New
    // registration rollback images have never been submitted and need no wait.
    impl_->Erase(iterator->second);
    impl_->images.erase(iterator);
}

RasterResult VulkanDevice::Draw(const RasterDraw& draw) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto micros = [](auto begin, auto end) { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()); };
    auto& gpu = *impl_;
    const bool tessellated = draw.rectangle || draw.quad;
    const std::uint64_t pixels = std::uint64_t{draw.width} * draw.height;
    if (!draw.width || !draw.height || draw.width > 16384 || draw.height > 16384 ||
        pixels > 32 * 1024 * 1024 || draw.color.size() != pixels * 4 || draw.vertex.empty() || draw.pixel.empty() ||
        !draw.vertices || !draw.instances || draw.buffers.size() > 32 || draw.textures.size() > 16 || draw.samplers.size() > 16 || draw.color_mask > 15 || draw.depth_compare > 7 ||
        (draw.rectangle && draw.quad) || (draw.quad && (draw.cull_front || draw.cull_back)) ||
        (tessellated && (draw.vertices % (draw.quad ? 4 : 3) || draw.rectangle_control.empty() || draw.rectangle_evaluation.empty()))) {
        throw std::runtime_error("Invalid raster draw extent/program");
    }
    for (const auto value : draw.viewport) { if (!std::isfinite(value)) { throw std::runtime_error("Non-finite viewport"); } }
    if (draw.viewport[2] <= 0 || draw.viewport[3] == 0 || draw.viewport[4] < 0 || draw.viewport[4] > 1 || draw.viewport[5] < 0 || draw.viewport[5] > 1 ||
        draw.viewport[2] > gpu.properties.limits.maxViewportDimensions[0] || std::abs(draw.viewport[3]) > gpu.properties.limits.maxViewportDimensions[1] ||
        draw.viewport[0] < gpu.properties.limits.viewportBoundsRange[0] || draw.viewport[0] + draw.viewport[2] > gpu.properties.limits.viewportBoundsRange[1] ||
        std::min(draw.viewport[1], draw.viewport[1] + draw.viewport[3]) < gpu.properties.limits.viewportBoundsRange[0] ||
        std::max(draw.viewport[1], draw.viewport[1] + draw.viewport[3]) > gpu.properties.limits.viewportBoundsRange[1] ||
        draw.scissor[0] < 0 || draw.scissor[1] < 0 || draw.scissor[2] < 0 || draw.scissor[3] < 0 ||
        std::uint64_t{static_cast<unsigned>(draw.scissor[0])} + static_cast<unsigned>(draw.scissor[2]) > draw.width ||
        std::uint64_t{static_cast<unsigned>(draw.scissor[1])} + static_cast<unsigned>(draw.scissor[3]) > draw.height) {
        throw std::runtime_error("Unsupported raster viewport/scissor");
    }
    const bool has_depth = draw.depth || draw.stencil;
    if ((draw.depth && !draw.clear_depth && draw.depth_data.size() != pixels) ||
        (draw.stencil && !draw.clear_stencil && draw.stencil_data.size() != pixels)) { throw std::runtime_error("Depth/stencil initialization extent mismatch"); }
    // Reverse destruction order plus a last-resort drain on exceptional paths:
    // no buffer/module/attachment can be destroyed while the queue still uses it.
    struct Scope {
        Impl& gpu;
        bool pending{};
        std::vector<std::function<void()>> cleanup;
        void Retire() {
            if (pending) { gpu.wait_idle(gpu.device); }
            for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it) { (*it)(); }
            cleanup.clear(); pending = false;
        }
        ~Scope() { Retire(); }
    } scope{gpu, false, {}};
    const auto memory_type = [&](std::uint32_t bits, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags preferred = 0) {
        std::array<std::uint32_t, VK_MAX_MEMORY_TYPES> properties{};
        for (unsigned index = 0; index < gpu.memory.memoryTypeCount; ++index) {
            properties[index] = gpu.memory.memoryTypes[index].propertyFlags;
        }
        const auto index = SelectMemoryType(std::span{properties}.first(gpu.memory.memoryTypeCount), bits, flags, preferred);
        if (index != UINT32_MAX) { return index; }
        throw std::runtime_error("No compatible raster memory type");
    };
    struct Buffer {
        VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{}; VkDeviceSize size{}; VkMemoryPropertyFlags properties{};
        std::shared_ptr<Impl::CachedBuffer> lease{};
    };
    unsigned buffer_hits{}, buffer_creates{};
    const auto buffer = [&](VkDeviceSize bytes, VkBufferUsageFlags usage, const void* data) {
        std::shared_ptr<Impl::CachedBuffer> lease;
        for (auto it = gpu.buffers.begin(); it != gpu.buffers.end(); ++it) {
            // An outstanding lease belongs to this draw, or to pending work
            // on an exceptional path. Never overwrite it before retirement.
            if (it->use_count() == 1 && (*it)->usage == usage && (*it)->size >= bytes) {
                lease = *it; gpu.buffers.splice(gpu.buffers.begin(), gpu.buffers, it);
                ++buffer_hits; ++gpu.cache_stats.buffer_hits; break;
            }
        }
        if (!lease) {
            lease = std::make_shared<Impl::CachedBuffer>(); lease->owner = &gpu; lease->size = bytes; lease->usage = usage;
            VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; info.size = bytes;
            info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            Check(gpu.Device<PFN_vkCreateBuffer>("vkCreateBuffer")(gpu.device, &info, nullptr, &lease->handle), "vkCreateBuffer");
            VkMemoryRequirements requirements{};
            gpu.Device<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(gpu.device, lease->handle, &requirements);
            VkMemoryAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = memory_type(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0);
            lease->properties = gpu.memory.memoryTypes[allocate.memoryTypeIndex].propertyFlags;
            lease->allocation_bytes = requirements.size;
            Check(gpu.allocate_memory(gpu.device, &allocate, nullptr, &lease->memory), "vkAllocateMemory(buffer)");
            Check(gpu.Device<PFN_vkBindBufferMemory>("vkBindBufferMemory")(gpu.device, lease->handle, lease->memory, 0), "vkBindBufferMemory");
            Check(gpu.Device<PFN_vkMapMemory>("vkMapMemory")(gpu.device, lease->memory, 0, bytes, 0, &lease->mapped), "vkMapMemory");
            ++buffer_creates; ++gpu.cache_stats.buffer_creates;
            constexpr std::uint64_t limit = 256ull * 1024 * 1024;
            if (lease->allocation_bytes <= limit) {
                while (!gpu.buffers.empty() && (gpu.buffers.size() >= 64 || gpu.cache_stats.buffer_bytes > limit - lease->allocation_bytes)) {
                    gpu.cache_stats.buffer_bytes -= gpu.buffers.back()->allocation_bytes;
                    gpu.buffers.pop_back(); ++gpu.cache_stats.buffer_evictions;
                }
                gpu.buffers.push_front(lease); gpu.cache_stats.buffer_bytes += lease->allocation_bytes;
            }
        }
        scope.cleanup.emplace_back([lease] {});
        // Reuse concerns storage only: every upload takes a fresh snapshot.
        if (data) { std::memcpy(lease->mapped, data, static_cast<std::size_t>(bytes)); }
        return Buffer{lease->handle, lease->memory, lease->mapped, bytes, lease->properties, lease};
    };
    struct Attachment {
        VkImage handle{}; VkImageView view{}; std::uint64_t allocation_bytes{};
        std::shared_ptr<Impl::CachedAttachment> cached{};
        bool initialized{}, color_reuse{}, depth_reuse{}, stencil_reuse{};
    };
    unsigned attachment_hits{}, attachment_creates{};
    const auto attachment = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, unsigned width = 0, unsigned height = 0, const RasterTexture* texture = nullptr) {
        const auto extent_width = width ? width : draw.width, extent_height = height ? height : draw.height;
        if (!texture) {
            auto selected = gpu.attachments.end(); std::uint64_t best{}; std::array<bool, 3> selected_same{};
            const auto matches = [&](const Impl::CachedAttachment& old) {
                std::array<bool, 3> result{};
                if (draw.reuse_attachment_contents && old.initialized) {
                    const auto exact = [](const auto& stored, const auto& readback, std::uint64_t bytes, const auto current) {
                        if (current.empty()) { return false; }
                        if (readback) { return bytes == current.size_bytes() && std::memcmp(readback->mapped, current.data(), current.size_bytes()) == 0; }
                        return stored.size() == current.size() && std::memcmp(stored.data(), current.data(), current.size_bytes()) == 0;
                    };
                    if (aspect & VK_IMAGE_ASPECT_COLOR_BIT) { result[0] = exact(old.color, old.color_readback, old.color_bytes, draw.color); }
                    if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && draw.depth && !draw.clear_depth) { result[1] = exact(old.depth, old.depth_readback, old.depth_bytes, draw.depth_data); }
                    if ((aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && draw.stencil && !draw.clear_stencil) { result[2] = exact(old.stencil, old.stencil_readback, old.stencil_bytes, draw.stencil_data); }
                }
                return result;
            };
            for (auto it = gpu.attachments.begin(); it != gpu.attachments.end(); ++it) {
                const auto& old = **it;
                if (it->use_count() == 1 && old.width == extent_width && old.height == extent_height &&
                    old.format == format && old.usage == usage && old.aspect == aspect) {
                    const auto same = matches(old);
                    const auto score = (same[0] ? draw.color.size_bytes() : 0) + (same[1] ? draw.depth_data.size_bytes() : 0) + (same[2] ? draw.stencil_data.size_bytes() : 0);
                    if (selected == gpu.attachments.end() || score > best) { selected = it; best = score; selected_same = same; }
                }
            }
            if (selected != gpu.attachments.end()) {
                auto lease = *selected; const auto same = selected_same;
                gpu.attachments.splice(gpu.attachments.begin(), gpu.attachments, selected);
                scope.cleanup.emplace_back([lease] {}); ++attachment_hits; ++gpu.cache_stats.attachment_hits;
                Attachment result{lease->image, lease->view, lease->bytes}; result.cached = lease;
                result.initialized = lease->initialized; result.color_reuse = same[0]; result.depth_reuse = same[1]; result.stencil_reuse = same[2];
                // Any exception after acquisition must conservatively forget
                // contents/layout; pending GPU work is drained by Scope.
                lease->initialized = false;
                // The exact comparison is complete. Drop old proof leases so
                // their storage can be reused by THIS draw, never before it.
                lease->ReleaseReadbacks();
                return result;
            }
        }
        const auto cleanup_begin = scope.cleanup.size();
        Attachment result{};
        VkImageCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {extent_width, extent_height, 1};
        info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(gpu.create_image(gpu.device, &info, nullptr, &result.handle), "vkCreateImage(raster)");
        scope.cleanup.emplace_back([owner = &gpu, handle = result.handle] { owner->destroy_image(owner->device, handle, nullptr); });
        VkMemoryRequirements requirements{}; gpu.image_requirements(gpu.device, result.handle, &requirements);
        VkMemoryAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory memory{};
        Check(gpu.allocate_memory(gpu.device, &allocate, nullptr, &memory), "vkAllocateMemory(raster)");
        result.allocation_bytes = requirements.size;
        auto destroy_image = std::move(scope.cleanup.back()); scope.cleanup.pop_back();
        scope.cleanup.emplace_back([owner = &gpu, memory] { owner->free_memory(owner->device, memory, nullptr); }); scope.cleanup.push_back(std::move(destroy_image));
        Check(gpu.bind_image(gpu.device, result.handle, memory, 0), "vkBindImageMemory(raster)");
        VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = result.handle; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
        if (texture) {
            const auto component = [](unsigned value) {
                switch (value) {
                case 0: return VK_COMPONENT_SWIZZLE_ZERO;
                case 1: return VK_COMPONENT_SWIZZLE_ONE;
                case 4: return VK_COMPONENT_SWIZZLE_R;
                case 5: return VK_COMPONENT_SWIZZLE_G;
                case 6: return VK_COMPONENT_SWIZZLE_B;
                case 7: return VK_COMPONENT_SWIZZLE_A;
                default: throw std::runtime_error("Invalid sampled-image swizzle");
                }
            };
            view.components = {component(texture->components[0]), component(texture->components[1]), component(texture->components[2]), component(texture->components[3])};
        }
        view.subresourceRange = {aspect, 0, 1, 0, 1};
        Check(gpu.create_view(gpu.device, &view, nullptr, &result.view), "vkCreateImageView(raster)");
        scope.cleanup.emplace_back([owner = &gpu, handle = result.view] { owner->destroy_view(owner->device, handle, nullptr); });
        if (!texture) {
            auto lease = std::make_shared<Impl::CachedAttachment>();
            lease->width = extent_width; lease->height = extent_height; lease->format = format; lease->usage = usage; lease->aspect = aspect;
            lease->image = result.handle; lease->view = result.view; lease->bytes = result.allocation_bytes;
            lease->cleanup.reserve(scope.cleanup.size() - cleanup_begin);
            for (auto index = cleanup_begin; index < scope.cleanup.size(); ++index) { lease->cleanup.push_back(std::move(scope.cleanup[index])); }
            scope.cleanup.resize(cleanup_begin);
            ++attachment_creates; ++gpu.cache_stats.attachment_creates;
            gpu.attachments.push_front(lease); gpu.TrimAttachments(); result.cached = lease;
            scope.cleanup.emplace_back([lease] {});
        }
        return result;
    };
    const auto format = draw.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const auto color = attachment(format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Attachment depth{};
    if (has_depth) { depth = attachment(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT); }
    const auto color_upload = color.color_reuse ? Buffer{} : buffer(pixels * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, draw.color.data());
    const auto color_readback = buffer(pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
    Buffer depth_upload{}, depth_readback{}, stencil_upload{}, stencil_readback{};
    if (has_depth) {
        if (draw.depth) { depth_readback = buffer(pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr); }
        if (draw.stencil) { stencil_readback = buffer(pixels, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr); }
        if (!draw.clear_depth && draw.depth && !depth.depth_reuse) { depth_upload = buffer(pixels * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, draw.depth_data.data()); }
        if (!draw.clear_stencil && draw.stencil && !depth.stencil_reuse) { stencil_upload = buffer(pixels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, draw.stencil_data.data()); }
    }
    std::vector<Buffer> storage;
    std::vector<Attachment> texture_images;
    std::vector<std::shared_ptr<Impl::CachedTexture>> texture_leases;
    std::vector<Buffer> texture_uploads;
    unsigned texture_hits{}, uploaded_textures{};
    std::vector<VkSampler> texture_samplers;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& entry : draw.buffers) {
        if (entry.bytes.empty() || entry.bytes.size() > gpu.properties.limits.maxStorageBufferRange ||
            std::any_of(bindings.begin(), bindings.end(), [&](const auto& old) { return old.binding == entry.binding; })) {
            throw std::runtime_error("Invalid or colliding shader buffer binding");
        }
        storage.push_back(buffer(entry.bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, entry.bytes.data()));
        bindings.push_back({entry.binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            entry.vertex ? VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT} : VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT}, nullptr});
    }
    const auto add_binding = [&](unsigned binding, bool vertex, VkDescriptorType type) {
        if (std::any_of(bindings.begin(), bindings.end(), [&](const auto& old) { return old.binding == binding; })) { throw std::runtime_error("Colliding shader image/sampler binding"); }
        bindings.push_back({binding, type, 1, vertex ? VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT} : VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT}, nullptr});
    };
    for (const auto& entry : draw.textures) {
        if (!entry.width || !entry.height || entry.width > gpu.properties.limits.maxImageDimension2D || entry.height > gpu.properties.limits.maxImageDimension2D ||
            entry.pixels.size() != std::uint64_t{entry.width} * entry.height * 4 ||
            (entry.snapshot && (entry.snapshot->data() != entry.pixels.data() || entry.snapshot->size() != entry.pixels.size()))) {
            throw std::runtime_error("Invalid texture upload extent/snapshot");
        }
        add_binding(entry.binding, entry.vertex, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        std::shared_ptr<Impl::CachedTexture> texture;
        for (auto it = gpu.texture_cache.begin(); it != gpu.texture_cache.end(); ++it) {
            const auto& old = **it;
            const auto old_pixels = old.snapshot ? std::span<const std::uint8_t>{*old.snapshot} : std::span<const std::uint8_t>{old.pixels};
            if (old.width == entry.width && old.height == entry.height && old.srgb == entry.srgb && old.components == entry.components &&
                // Immutable snapshots need no cross-snapshot content dedup.
                // Native decode already validated their current guest bytes.
                ((entry.snapshot && old.snapshot) ? entry.snapshot == old.snapshot :
                    std::equal(entry.pixels.begin(), entry.pixels.end(), old_pixels.begin(), old_pixels.end()))) {
                texture = *it; gpu.texture_cache.splice(gpu.texture_cache.begin(), gpu.texture_cache, it); break;
            }
        }
        if (!texture) {
            texture = std::make_shared<Impl::CachedTexture>(); texture->width = entry.width; texture->height = entry.height;
            texture->srgb = entry.srgb; texture->components = entry.components; texture->snapshot = entry.snapshot;
            if (!texture->snapshot) { texture->pixels.assign(entry.pixels.begin(), entry.pixels.end()); }
            const auto cleanup_begin = scope.cleanup.size();
            const auto image = attachment(entry.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, entry.width, entry.height, &entry);
            texture->image = image.handle; texture->view = image.view; texture->bytes = image.allocation_bytes + entry.pixels.size();
            texture->cleanup.reserve(scope.cleanup.size() - cleanup_begin);
            for (auto index = cleanup_begin; index < scope.cleanup.size(); ++index) { texture->cleanup.push_back(std::move(scope.cleanup[index])); }
            scope.cleanup.resize(cleanup_begin);
            constexpr std::uint64_t maximum_bytes = 256 * 1024 * 1024;
            if (texture->bytes <= maximum_bytes) {
                while (!gpu.texture_cache.empty() && (gpu.texture_cache.size() >= 64 || gpu.cache_stats.bytes > maximum_bytes - texture->bytes)) {
                    gpu.cache_stats.bytes -= gpu.texture_cache.back()->bytes; gpu.texture_cache.pop_back(); ++gpu.cache_stats.evictions;
                }
                gpu.texture_cache.push_front(texture); gpu.cache_stats.bytes += texture->bytes;
            }
        }
        if (texture->initialized) { ++texture_hits; ++gpu.cache_stats.texture_hits; }
        const bool staged = std::find(texture_leases.begin(), texture_leases.end(), texture) != texture_leases.end();
        texture_images.push_back({texture->image, texture->view, 0});
        // Scope keeps the lease until after any exceptional pending-work drain;
        // local vectors may be destroyed earlier during stack unwinding.
        scope.cleanup.emplace_back([texture] {}); texture_leases.push_back(texture);
        if (!texture->initialized && !staged) {
            texture_uploads.push_back(buffer(entry.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, entry.pixels.data()));
            ++uploaded_textures; ++gpu.cache_stats.texture_uploads;
        } else { texture_uploads.emplace_back(); }
    }
    const auto clamp = [](unsigned value) {
        switch (value) {
        case 0: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case 1: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 2: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 6: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: throw std::runtime_error("Unsupported sampler clamp mode");
        }
    };
    for (const auto& entry : draw.samplers) {
        if (!std::isfinite(entry.bias) || !std::isfinite(entry.minimum_lod) || !std::isfinite(entry.maximum_lod) ||
            std::abs(entry.bias) > gpu.properties.limits.maxSamplerLodBias || entry.minimum_lod < 0 || entry.minimum_lod > entry.maximum_lod || entry.border > 2) {
            throw std::runtime_error("Invalid sampler LOD/border configuration");
        }
        add_binding(entry.binding, entry.vertex, VK_DESCRIPTOR_TYPE_SAMPLER);
        VkSamplerCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = entry.linear_mag ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; info.minFilter = entry.linear_min ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = entry.linear_mip ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = clamp(entry.clamp[0]); info.addressModeV = clamp(entry.clamp[1]); info.addressModeW = clamp(entry.clamp[2]);
        info.mipLodBias = entry.bias; info.minLod = entry.minimum_lod; info.maxLod = entry.maximum_lod;
        info.borderColor = entry.border == 0 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK : entry.border == 1 ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VkSampler sampler{};
        Check(gpu.Device<PFN_vkCreateSampler>("vkCreateSampler")(gpu.device, &info, nullptr, &sampler), "vkCreateSampler");
        scope.cleanup.emplace_back([&, sampler] { gpu.Device<PFN_vkDestroySampler>("vkDestroySampler")(gpu.device, sampler, nullptr); });
        texture_samplers.push_back(sampler);
    }
    const auto resources_ready = Clock::now();
    auto pipeline_key = PipelineKey(draw, bindings);
    std::shared_ptr<Impl::CachedPipeline> pipeline_lease;
    for (auto it = gpu.pipelines.begin(); it != gpu.pipelines.end(); ++it) {
        if ((*it)->key == pipeline_key) { pipeline_lease = *it; gpu.pipelines.splice(gpu.pipelines.begin(), gpu.pipelines, it); break; }
    }
    const bool pipeline_hit = static_cast<bool>(pipeline_lease);
    if (pipeline_hit) { ++gpu.cache_stats.pipeline_hits; }
    else { pipeline_lease = std::make_shared<Impl::CachedPipeline>(); pipeline_lease->owner = &gpu; pipeline_lease->key = std::move(pipeline_key); }
    // Registered before descriptor pools/framebuffers/commands, so those
    // retire first. An exceptional pending draw drains before any lease dies.
    scope.cleanup.emplace_back([pipeline_lease] {});
    auto& descriptor_layout = pipeline_lease->descriptors;
    VkDescriptorSetLayoutCreateInfo descriptor_info{}; descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_info.bindingCount = static_cast<unsigned>(bindings.size()); descriptor_info.pBindings = bindings.data();
    if (!pipeline_hit) { Check(gpu.Device<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(gpu.device, &descriptor_info, nullptr, &descriptor_layout), "vkCreateDescriptorSetLayout"); }
    VkDescriptorPool descriptor_pool{};
    const std::array<VkDescriptorPoolSize, 3> pool_sizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, std::max(1u, static_cast<unsigned>(storage.size()))},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, std::max(1u, static_cast<unsigned>(texture_images.size()))},
        {VK_DESCRIPTOR_TYPE_SAMPLER, std::max(1u, static_cast<unsigned>(texture_samplers.size()))}}};
    VkDescriptorPoolCreateInfo pool_info{}; pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1; pool_info.poolSizeCount = static_cast<unsigned>(pool_sizes.size()); pool_info.pPoolSizes = pool_sizes.data();
    Check(gpu.Device<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(gpu.device, &pool_info, nullptr, &descriptor_pool), "vkCreateDescriptorPool");
    scope.cleanup.emplace_back([&, handle = descriptor_pool] { gpu.Device<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(gpu.device, handle, nullptr); });
    VkDescriptorSet descriptor_set{};
    VkDescriptorSetAllocateInfo set_info{}; set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = descriptor_pool; set_info.descriptorSetCount = 1; set_info.pSetLayouts = &descriptor_layout;
    Check(gpu.Device<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(gpu.device, &set_info, &descriptor_set), "vkAllocateDescriptorSets");
    for (unsigned index = 0; index < storage.size(); ++index) {
        VkDescriptorBufferInfo storage_info{storage[index].handle, 0, storage[index].size};
        VkWriteDescriptorSet write{}; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_set; write.dstBinding = bindings[index].binding; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &storage_info;
        gpu.Device<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(gpu.device, 1, &write, 0, nullptr);
    }
    for (unsigned index = 0; index < texture_images.size() + texture_samplers.size(); ++index) {
        const bool image = index < texture_images.size(); const auto resource = image ? index : index - static_cast<unsigned>(texture_images.size());
        VkDescriptorImageInfo descriptor{}; descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (image) { descriptor.imageView = texture_images[resource].view; } else { descriptor.sampler = texture_samplers[resource]; }
        VkWriteDescriptorSet write{}; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; write.dstSet = descriptor_set;
        write.dstBinding = image ? draw.textures[resource].binding : draw.samplers[resource].binding; write.descriptorCount = 1;
        write.descriptorType = image ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER; write.pImageInfo = &descriptor;
        gpu.Device<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(gpu.device, 1, &write, 0, nullptr);
    }
    VkPushConstantRange push{VK_SHADER_STAGE_ALL_GRAPHICS, 0, 56};
    auto& layout = pipeline_lease->layout;
    VkPipelineLayoutCreateInfo layout_info{}; layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1; layout_info.pSetLayouts = &descriptor_layout; layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &push;
    if (!pipeline_hit) { Check(gpu.Device<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(gpu.device, &layout_info, nullptr, &layout), "vkCreatePipelineLayout"); }
    std::array<VkAttachmentDescription, 2> descriptions{};
    descriptions[0].format = format; descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
    descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    descriptions[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; descriptions[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    descriptions[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT; descriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
    descriptions[1].loadOp = draw.clear_depth || !draw.depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    descriptions[1].stencilLoadOp = draw.clear_stencil || !draw.stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    descriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    descriptions[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; descriptions[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkAttachmentReference color_reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth_reference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color_reference; subpass.pDepthStencilAttachment = has_depth ? &depth_reference : nullptr;
    const std::array<VkSubpassDependency, 2> dependencies{{
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0}}};
    auto& render_pass = pipeline_lease->render_pass;
    VkRenderPassCreateInfo render_info{}; render_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_info.attachmentCount = has_depth ? 2 : 1; render_info.pAttachments = descriptions.data();
    render_info.subpassCount = 1; render_info.pSubpasses = &subpass; render_info.dependencyCount = 2; render_info.pDependencies = dependencies.data();
    if (!pipeline_hit) { Check(gpu.Device<PFN_vkCreateRenderPass>("vkCreateRenderPass")(gpu.device, &render_info, nullptr, &render_pass), "vkCreateRenderPass"); }
    const std::array views{color.view, depth.view};
    VkFramebuffer framebuffer{};
    VkFramebufferCreateInfo framebuffer_info{}; framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass; framebuffer_info.attachmentCount = has_depth ? 2 : 1; framebuffer_info.pAttachments = views.data();
    framebuffer_info.width = draw.width; framebuffer_info.height = draw.height; framebuffer_info.layers = 1;
    Check(gpu.Device<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(gpu.device, &framebuffer_info, nullptr, &framebuffer), "vkCreateFramebuffer");
    scope.cleanup.emplace_back([&, handle = framebuffer] { gpu.Device<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(gpu.device, handle, nullptr); });
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    const auto shader = [&](std::span<const std::uint32_t> words, VkShaderStageFlagBits stage) {
        VkShaderModule module{};
        VkShaderModuleCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; info.codeSize = words.size_bytes(); info.pCode = words.data();
        Check(gpu.Device<PFN_vkCreateShaderModule>("vkCreateShaderModule")(gpu.device, &info, nullptr, &module), "vkCreateShaderModule");
        scope.cleanup.emplace_back([&, module] { gpu.Device<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(gpu.device, module, nullptr); });
        VkPipelineShaderStageCreateInfo entry{}; entry.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        entry.stage = stage; entry.module = module; entry.pName = "main"; stages.push_back(entry);
    };
    if (!pipeline_hit) {
        shader(draw.vertex, VK_SHADER_STAGE_VERTEX_BIT);
        if (tessellated) { shader(draw.rectangle_control, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT); shader(draw.rectangle_evaluation, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT); }
        shader(draw.pixel, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{}; vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{}; assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = tessellated ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineTessellationStateCreateInfo tessellation{}; tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO; tessellation.patchControlPoints = draw.quad ? 4 : 3;
    VkViewport viewport{draw.viewport[0], draw.viewport[1], draw.viewport[2], draw.viewport[3], draw.viewport[4], draw.viewport[5]};
    VkRect2D scissor{{draw.scissor[0], draw.scissor[1]}, {static_cast<unsigned>(draw.scissor[2]), static_cast<unsigned>(draw.scissor[3])}};
    VkPipelineViewportStateCreateInfo viewport_info{}; viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = viewport_info.scissorCount = 1; viewport_info.pViewports = &viewport; viewport_info.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{}; raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0f;
    raster.cullMode = draw.rectangle ? VK_CULL_MODE_NONE : (draw.cull_front ? VK_CULL_MODE_FRONT_BIT : 0) | (draw.cull_back ? VK_CULL_MODE_BACK_BIT : 0);
    raster.frontFace = draw.clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo multisample{}; multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state{}; depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    constexpr std::array compares{VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS};
    depth_state.depthTestEnable = draw.depth && !draw.clear_depth;
    depth_state.depthWriteEnable = draw.depth_write && !draw.clear_depth; depth_state.depthCompareOp = compares[draw.depth_compare];
    depth_state.stencilTestEnable = draw.stencil && !draw.clear_stencil;
    const auto stencil_state = [&](const RasterStencil& state) {
        constexpr std::array operations{VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
            VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP};
        if (state.fail > 7 || state.pass > 7 || state.depth_fail > 7 || state.compare > 7 || state.reference > 255 || state.compare_mask > 255 || state.write_mask > 255) {
            throw std::runtime_error("Invalid raster stencil configuration");
        }
        return VkStencilOpState{operations[state.fail], operations[state.pass], operations[state.depth_fail], compares[state.compare], state.compare_mask, state.write_mask, state.reference};
    };
    depth_state.front = stencil_state(draw.stencil_front); depth_state.back = stencil_state(draw.stencil_back);
    const auto blend_factor = [](BlendFactor value) {
        switch (value) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SourceColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::InverseSourceColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::InverseSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DestinationAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::InverseDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::InverseDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SourceAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::InverseConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::InverseConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        }
        throw std::runtime_error("Invalid raster blend factor");
    };
    const auto blend_operation = [](BlendOperation value) {
        switch (value) {
        case BlendOperation::Add: return VK_BLEND_OP_ADD;
        case BlendOperation::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOperation::Minimum: return VK_BLEND_OP_MIN;
        case BlendOperation::Maximum: return VK_BLEND_OP_MAX;
        case BlendOperation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        }
        throw std::runtime_error("Invalid raster blend operation");
    };
    VkPipelineColorBlendAttachmentState blend_attachment{}; blend_attachment.colorWriteMask = draw.color_mask;
    blend_attachment.blendEnable = draw.blend.enabled;
    blend_attachment.srcColorBlendFactor = blend_factor(draw.blend.source_color);
    blend_attachment.dstColorBlendFactor = blend_factor(draw.blend.destination_color);
    blend_attachment.colorBlendOp = blend_operation(draw.blend.color);
    blend_attachment.srcAlphaBlendFactor = blend_factor(draw.blend.source_alpha);
    blend_attachment.dstAlphaBlendFactor = blend_factor(draw.blend.destination_alpha);
    blend_attachment.alphaBlendOp = blend_operation(draw.blend.alpha);
    VkPipelineColorBlendStateCreateInfo blend{}; blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1; blend.pAttachments = &blend_attachment;
    std::copy(draw.blend.constants.begin(), draw.blend.constants.end(), blend.blendConstants);
    VkGraphicsPipelineCreateInfo pipeline_info{}; pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<unsigned>(stages.size()); pipeline_info.pStages = stages.data(); pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly; pipeline_info.pTessellationState = tessellated ? &tessellation : nullptr;
    pipeline_info.pViewportState = &viewport_info; pipeline_info.pRasterizationState = &raster; pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_state; pipeline_info.pColorBlendState = &blend; pipeline_info.layout = layout; pipeline_info.renderPass = render_pass;
    auto& pipeline = pipeline_lease->pipeline;
    if (!pipeline_hit) {
        Check(gpu.Device<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(gpu.device, gpu.pipeline_cache, 1, &pipeline_info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
        ++gpu.cache_stats.pipeline_creates;
        constexpr std::uint64_t maximum_key_bytes = 16ull * 1024 * 1024;
        const auto bytes = pipeline_lease->key.size() * sizeof(std::uint32_t);
        if (bytes <= maximum_key_bytes) {
            while (!gpu.pipelines.empty() && (gpu.pipelines.size() >= 64 || gpu.cache_stats.pipeline_key_bytes > maximum_key_bytes - bytes)) {
                gpu.cache_stats.pipeline_key_bytes -= gpu.pipelines.back()->key.size() * sizeof(std::uint32_t);
                gpu.pipelines.pop_back(); ++gpu.cache_stats.pipeline_evictions;
            }
            gpu.pipelines.push_front(pipeline_lease); gpu.cache_stats.pipeline_key_bytes += bytes;
        }
    }
    const auto pipeline_ready = Clock::now();
    VkCommandPool command_pool{};
    VkCommandPoolCreateInfo command_pool_info{}; command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; command_pool_info.queueFamilyIndex = gpu.queue_family;
    Check(gpu.Device<PFN_vkCreateCommandPool>("vkCreateCommandPool")(gpu.device, &command_pool_info, nullptr, &command_pool), "vkCreateCommandPool");
    scope.cleanup.emplace_back([&, handle = command_pool] { gpu.Device<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(gpu.device, handle, nullptr); });
    VkCommandBuffer command{};
    VkCommandBufferAllocateInfo command_info{}; command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; command_info.commandPool = command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
    Check(gpu.Device<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(gpu.device, &command_info, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(gpu.Device<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command, &begin), "vkBeginCommandBuffer");
    const auto barrier = [&](VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout,
        VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags src_access, VkAccessFlags dst_access) {
        VkImageMemoryBarrier entry{}; entry.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        entry.srcAccessMask = src_access; entry.dstAccessMask = dst_access; entry.oldLayout = old_layout; entry.newLayout = new_layout;
        entry.srcQueueFamilyIndex = entry.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; entry.image = image; entry.subresourceRange = {aspect, 0, 1, 0, 1};
        gpu.Device<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command, src, dst, 0, 0, nullptr, 0, nullptr, 1, &entry);
    };
    const auto copy_to_image = [&](const Buffer& source, VkImage image, VkImageAspectFlags aspect, unsigned width = 0, unsigned height = 0) {
        VkBufferImageCopy copy{}; copy.imageSubresource = {aspect, 0, 0, 1}; copy.imageExtent = {width ? width : draw.width, height ? height : draw.height, 1};
        gpu.Device<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(command, source.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    };
    for (unsigned index = 0; index < texture_images.size(); ++index) {
        if (!texture_uploads[index].handle) { continue; }
        const auto image = texture_images[index].handle;
        barrier(image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        copy_to_image(texture_uploads[index], image, VK_IMAGE_ASPECT_COLOR_BIT, draw.textures[index].width, draw.textures[index].height);
        barrier(image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    const auto preserves = [](const Attachment& value) { return value.color_reuse || value.depth_reuse || value.stencil_reuse; };
    const auto old_layout = [&](const Attachment& value) { return preserves(value) ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED; };
    const auto source_stage = [&](const Attachment& value) { return preserves(value) ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; };
    const auto source_access = [&](const Attachment& value) -> VkAccessFlags { return preserves(value) ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0; };
    if (color_upload.handle) {
        barrier(color.handle, VK_IMAGE_ASPECT_COLOR_BIT, old_layout(color), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            source_stage(color), VK_PIPELINE_STAGE_TRANSFER_BIT, source_access(color), VK_ACCESS_TRANSFER_WRITE_BIT);
        copy_to_image(color_upload, color.handle, VK_IMAGE_ASPECT_COLOR_BIT);
        barrier(color.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    } else {
        barrier(color.handle, VK_IMAGE_ASPECT_COLOR_BIT, old_layout(color), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            source_stage(color), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, source_access(color), VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    if (has_depth) {
        if (depth_upload.handle || stencil_upload.handle) {
            barrier(depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, old_layout(depth), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                source_stage(depth), VK_PIPELINE_STAGE_TRANSFER_BIT, source_access(depth), VK_ACCESS_TRANSFER_WRITE_BIT);
            if (depth_upload.handle) { copy_to_image(depth_upload, depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT); }
            if (stencil_upload.handle) { copy_to_image(stencil_upload, depth.handle, VK_IMAGE_ASPECT_STENCIL_BIT); }
            barrier(depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        } else {
            barrier(depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, old_layout(depth), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                source_stage(depth), VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, source_access(depth),
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        }
    }
    std::array<VkClearValue, 2> clear{}; clear[1].depthStencil = {draw.depth_clear, draw.stencil_clear};
    VkRenderPassBeginInfo pass{}; pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; pass.renderPass = render_pass; pass.framebuffer = framebuffer;
    pass.renderArea.extent = {draw.width, draw.height}; pass.clearValueCount = has_depth ? 2 : 1; pass.pClearValues = clear.data();
    gpu.Device<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    gpu.Device<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    gpu.Device<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor_set, 0, nullptr);
    gpu.Device<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command, layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 56, draw.push_constants.data());
    gpu.Device<PFN_vkCmdDraw>("vkCmdDraw")(command, draw.vertices, draw.instances, 0, 0);
    gpu.Device<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(command);
    const auto read_image = [&](VkImage image, const Buffer& destination, VkImageAspectFlags aspect) {
        VkBufferImageCopy copy{}; copy.imageSubresource = {aspect, 0, 0, 1}; copy.imageExtent = {draw.width, draw.height, 1};
        gpu.Device<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.handle, 1, &copy);
    };
    read_image(color.handle, color_readback, VK_IMAGE_ASPECT_COLOR_BIT);
    if (draw.depth) { read_image(depth.handle, depth_readback, VK_IMAGE_ASPECT_DEPTH_BIT); }
    if (draw.stencil) { read_image(depth.handle, stencil_readback, VK_IMAGE_ASPECT_STENCIL_BIT); }
    VkMemoryBarrier host_barrier{}; host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    gpu.Device<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, nullptr, 0, nullptr);
    Check(gpu.Device<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command), "vkEndCommandBuffer");
    VkFence fence{};
    VkFenceCreateInfo fence_info{}; fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    Check(gpu.Device<PFN_vkCreateFence>("vkCreateFence")(gpu.device, &fence_info, nullptr, &fence), "vkCreateFence");
    scope.cleanup.emplace_back([&, handle = fence] { gpu.Device<PFN_vkDestroyFence>("vkDestroyFence")(gpu.device, handle, nullptr); });
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    const auto commands_ready = Clock::now();
    Check(gpu.Device<PFN_vkQueueSubmit>("vkQueueSubmit")(gpu.queue, 1, &submit, fence), "vkQueueSubmit"); scope.pending = true;
    const auto submitted = Clock::now();
    Check(gpu.Device<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &fence, VK_TRUE, 5'000'000'000ull), "vkWaitForFences(raster)"); scope.pending = false;
    const auto completed = Clock::now();
    for (const auto& texture : texture_leases) { texture->initialized = true; }
    RasterResult result; result.color.resize(static_cast<std::size_t>(pixels * 4));
    result.texture_cache_hits = texture_hits; result.texture_uploads = uploaded_textures;
    result.pipeline_cache_hit = pipeline_hit;
    result.buffer_cache_hits = buffer_hits; result.buffer_creates = buffer_creates;
    result.attachment_cache_hits = attachment_hits; result.attachment_creates = attachment_creates;
    result.color_content_reused = color.color_reuse; result.depth_content_reused = depth.depth_reuse; result.stencil_content_reused = depth.stencil_reuse;
    result.color_upload_bytes = color_upload.size; result.depth_upload_bytes = depth_upload.size; result.stencil_upload_bytes = stencil_upload.size;
    gpu.cache_stats.attachment_content_hits += static_cast<unsigned>(color.color_reuse) + static_cast<unsigned>(depth.depth_reuse) + static_cast<unsigned>(depth.stencil_reuse);
    result.readback_host_cached = (color_readback.properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0;
    std::memcpy(result.color.data(), color_readback.mapped, result.color.size());
    if (draw.depth) {
        result.depth.resize(static_cast<std::size_t>(pixels));
        std::memcpy(result.depth.data(), depth_readback.mapped, result.depth.size() * 4);
    }
    if (draw.stencil) {
        result.stencil.resize(static_cast<std::size_t>(pixels));
        std::memcpy(result.stencil.data(), stencil_readback.mapped, result.stencil.size());
    }
    if (gpu.validation_errors.load()) { throw std::runtime_error("Vulkan validation reported a raster error; inspect stderr"); }
    const auto remember = [&](const Attachment& image, bool color_plane) {
        if (!image.cached) { return; }
        auto& saved = *image.cached;
        const auto desired = color_plane ? result.color.size() : result.depth.size() * sizeof(float) + result.stencil.size();
        const auto forget = [&] {
            std::vector<std::uint8_t>{}.swap(saved.color); std::vector<std::uint8_t>{}.swap(saved.stencil); std::vector<float>{}.swap(saved.depth);
            saved.ReleaseReadbacks();
        };
        if (draw.reuse_attachment_contents && saved.bytes + desired <= 128ull * 1024 * 1024) {
            try {
                const auto capture = [](const Buffer& source, const auto& current, auto& owned, auto& retained, std::uint64_t& bytes) {
                    retained.reset(); bytes = 0;
                    if (source.lease && (source.properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                        // Completed HOST_CACHED readback is already an exact
                        // CPU-readable snapshot. Its strong lease excludes it
                        // from the transient pool until comparison/invalidation.
                        retained = source.lease; bytes = current.size() * sizeof(current[0]);
                        std::decay_t<decltype(owned)>{}.swap(owned);
                    } else { owned = current; } // coherent noncached fallback
                };
                if (color_plane) { capture(color_readback, result.color, saved.color, saved.color_readback, saved.color_bytes); }
                else {
                    capture(depth_readback, result.depth, saved.depth, saved.depth_readback, saved.depth_bytes);
                    capture(stencil_readback, result.stencil, saved.stencil, saved.stencil_readback, saved.stencil_bytes);
                }
            } catch (const std::bad_alloc&) {
                // The optional CPU proof is not required to return a completed
                // GPU result. Forget it and upload on the next draw instead.
                forget(); ++result.attachment_snapshot_fallbacks; ++gpu.cache_stats.attachment_snapshot_fallbacks;
            }
        } else { forget(); }
        // Both aspects end in TRANSFER_SRC, including an unrequested aspect.
        // Only read-back, byte-exact requested planes can prove a later hit.
        saved.initialized = true;
    };
    remember(color, true); if (has_depth) { remember(depth, false); } gpu.TrimAttachments();
    const auto readback_ready = Clock::now();
    scope.Retire();
    result.resources_us = micros(started, resources_ready); result.pipeline_us = micros(resources_ready, pipeline_ready);
    result.commands_us = micros(pipeline_ready, commands_ready); result.submit_us = micros(commands_ready, submitted);
    result.wait_us = micros(submitted, completed); result.readback_us = micros(completed, readback_ready);
    result.cleanup_us = micros(readback_ready, Clock::now());
    return result;
}
RasterCacheStats VulkanDevice::CacheStats() const {
    auto stats = impl_->cache_stats; stats.entries = impl_->texture_cache.size(); stats.pipeline_entries = impl_->pipelines.size();
    stats.buffer_entries = impl_->buffers.size(); stats.attachment_entries = impl_->attachments.size(); return stats;
}
void VulkanDevice::TrimRasterCaches() {
    WaitIdle(); impl_->texture_cache.clear(); impl_->pipelines.clear(); impl_->buffers.clear(); impl_->attachments.clear();
    impl_->cache_stats.bytes = impl_->cache_stats.pipeline_key_bytes = impl_->cache_stats.buffer_bytes = 0;
    impl_->cache_stats.attachment_bytes = impl_->cache_stats.attachment_snapshot_bytes = 0;
}

PresentStatus VulkanDevice::Present(std::uint64_t id, std::span<const std::uint8_t> linear) {
    auto& gpu = *impl_;
    const auto found = gpu.images.find(id);
    if (found == gpu.images.end()) { throw std::runtime_error("Present image is not registered"); }
    auto& source = found->second;
    const auto& layout = source.layout;
    if (linear.size() != std::size_t{layout.width} * layout.height * 4) { throw std::runtime_error("Present upload extent mismatch"); }
    VkSurfaceCapabilitiesKHR caps{};
    Check(gpu.Instance<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(gpu.physical, gpu.surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) { throw std::runtime_error("Window surface cannot receive a transfer image"); }
    auto extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { extent = {std::clamp(layout.width, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(layout.height, caps.minImageExtent.height, caps.maxImageExtent.height)}; }
    if (!extent.width || !extent.height) { return PresentStatus::ZeroExtent; }
    if (gpu.swapchain && (gpu.swapchain_extent.width != extent.width || gpu.swapchain_extent.height != extent.height)) {
        // This synchronous upload path has already retired its previous
        // submission using a present fence or the compatibility queue drain.
        gpu.Device<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(gpu.device, gpu.swapchain, nullptr); gpu.swapchain = {};
    }
    if (!gpu.swapchain) {
        const auto enumerate_formats = gpu.Instance<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
        const auto formats = Enumerate<VkSurfaceFormatKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR", 1024,
            [&](auto* count, auto* data) { return enumerate_formats(gpu.physical, gpu.surface, count, data); });
        const auto selected = std::find_if(formats.begin(), formats.end(), [](const auto& entry) {
            return entry.format == VK_FORMAT_B8G8R8A8_SRGB && entry.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (selected == formats.end()) { throw std::runtime_error("An SDR BGRA sRGB window surface is required"); }
        VkSwapchainCreateInfoKHR info{}; info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = gpu.surface; info.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount) { info.minImageCount = std::min(info.minImageCount, caps.maxImageCount); }
        info.imageFormat = selected->format; info.imageColorSpace = selected->colorSpace; info.imageExtent = extent;
        info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const auto enumerate_modes = gpu.Instance<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
        const auto modes = Enumerate<VkPresentModeKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR", 1024,
            [&](auto* count, auto* data) { return enumerate_modes(gpu.physical, gpu.surface, count, data); });
        info.preTransform = caps.currentTransform;
        info.presentMode = cave::ps4::SelectPresentMode(gpu.vsync, std::span<const VkPresentModeKHR>(modes),
            VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR);
        info.clipped = VK_TRUE;
        for (const auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        }
        Check(gpu.Device<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(gpu.device, &info, nullptr, &gpu.swapchain), "vkCreateSwapchainKHR");
        const auto get_images = gpu.Device<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
        gpu.swapchain_images = Enumerate<VkImage>("vkGetSwapchainImagesKHR", 256,
            [&](auto* count, auto* data) { return get_images(gpu.device, gpu.swapchain, count, data); });
        gpu.swapchain_initialized.assign(gpu.swapchain_images.size(), false); gpu.swapchain_extent = extent;
        const auto mode_name = info.presentMode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO" :
            info.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : "MAILBOX";
        std::printf("{\"event\":\"native_graphics_swapchain\",\"width\":%u,\"height\":%u,\"images\":%zu,\"mode\":\"%s\",\"present_mode\":%u,\"vsync_requested\":%s,\"completion\":\"%s\"}\n",
            extent.width, extent.height, gpu.swapchain_images.size(), mode_name, unsigned(info.presentMode), gpu.vsync ? "true" : "false", gpu.present_fences ? "maintenance1_fence" : "synchronous_queue_drain");
    }
    struct Scope {
        Impl& gpu;
        VkFence pending{};
        bool submitted{}, queue_pending{};
        std::vector<std::function<void()>> cleanup;
        ~Scope() {
            // Exceptional paths must not free a present-wait semaphore merely
            // because vkQueuePresentKHR returned. Wait for the selected
            // completion mechanism; the process watchdog bounds a stuck driver.
            if (pending) { gpu.Device<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &pending, VK_TRUE, UINT64_MAX); }
            if (queue_pending) { gpu.Device<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(gpu.queue); }
            if (submitted) { gpu.wait_idle(gpu.device); }
            for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it) { (*it)(); }
        }
    } scope{gpu, {}, false, false, {}};
    const auto make_fence = [&] {
        VkFence fence{}; VkFenceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Check(gpu.Device<PFN_vkCreateFence>("vkCreateFence")(gpu.device, &info, nullptr, &fence), "vkCreateFence(present)");
        scope.cleanup.emplace_back([&, fence] { gpu.Device<PFN_vkDestroyFence>("vkDestroyFence")(gpu.device, fence, nullptr); }); return fence;
    };
    const auto acquired = make_fence(), completed = make_fence(), presented = make_fence();
    unsigned index{};
    const auto acquire = gpu.Device<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(gpu.device, gpu.swapchain, 1'000'000'000ull, VK_NULL_HANDLE, acquired, &index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        std::printf("{\"event\":\"native_graphics_present_retry\",\"phase\":\"acquire\",\"vk_result\":%d,\"swapchain_retired\":true}\n", static_cast<int>(acquire));
        gpu.Device<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(gpu.device, gpu.swapchain, nullptr); gpu.swapchain = {}; return PresentStatus::OutOfDate;
    }
    if (acquire != VK_SUBOPTIMAL_KHR) { Check(acquire, "vkAcquireNextImageKHR"); }
    Check(gpu.Device<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &acquired, VK_TRUE, 5'000'000'000ull), "vkWaitForFences(acquire)");
    if (index >= gpu.swapchain_images.size()) { throw std::runtime_error("Acquired image index exceeds swapchain"); }
    VkBuffer upload{}; VkDeviceMemory memory{}; void* mapped{};
    VkBufferCreateInfo buffer_info{}; buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; buffer_info.size = linear.size();
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(gpu.Device<PFN_vkCreateBuffer>("vkCreateBuffer")(gpu.device, &buffer_info, nullptr, &upload), "vkCreateBuffer(present)");
    scope.cleanup.emplace_back([&, upload] { gpu.Device<PFN_vkDestroyBuffer>("vkDestroyBuffer")(gpu.device, upload, nullptr); });
    VkMemoryRequirements requirements{}; gpu.Device<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(gpu.device, upload, &requirements);
    unsigned memory_type = UINT32_MAX;
    for (unsigned candidate = 0; candidate < gpu.memory.memoryTypeCount; ++candidate) {
        constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1u << candidate)) && (gpu.memory.memoryTypes[candidate].propertyFlags & flags) == flags) { memory_type = candidate; break; }
    }
    if (memory_type == UINT32_MAX) { throw std::runtime_error("No coherent present staging memory"); }
    VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memory_type;
    Check(gpu.allocate_memory(gpu.device, &allocation, nullptr, &memory), "vkAllocateMemory(present)");
    auto delete_buffer = std::move(scope.cleanup.back()); scope.cleanup.pop_back();
    scope.cleanup.emplace_back([&, memory] { gpu.free_memory(gpu.device, memory, nullptr); }); scope.cleanup.push_back(std::move(delete_buffer));
    Check(gpu.Device<PFN_vkBindBufferMemory>("vkBindBufferMemory")(gpu.device, upload, memory, 0), "vkBindBufferMemory(present)");
    Check(gpu.Device<PFN_vkMapMemory>("vkMapMemory")(gpu.device, memory, 0, linear.size(), 0, &mapped), "vkMapMemory(present)");
    std::memcpy(mapped, linear.data(), linear.size()); gpu.Device<PFN_vkUnmapMemory>("vkUnmapMemory")(gpu.device, memory);
    VkSemaphore ready{}; VkSemaphoreCreateInfo semaphore_info{}; semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Check(gpu.Device<PFN_vkCreateSemaphore>("vkCreateSemaphore")(gpu.device, &semaphore_info, nullptr, &ready), "vkCreateSemaphore(present)");
    scope.cleanup.emplace_back([&, ready] { gpu.Device<PFN_vkDestroySemaphore>("vkDestroySemaphore")(gpu.device, ready, nullptr); });
    VkCommandPool pool{}; VkCommandPoolCreateInfo pool_info{}; pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pool_info.queueFamilyIndex = gpu.queue_family;
    Check(gpu.Device<PFN_vkCreateCommandPool>("vkCreateCommandPool")(gpu.device, &pool_info, nullptr, &pool), "vkCreateCommandPool(present)");
    scope.cleanup.emplace_back([&, pool] { gpu.Device<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(gpu.device, pool, nullptr); });
    VkCommandBuffer command{}; VkCommandBufferAllocateInfo command_info{}; command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
    Check(gpu.Device<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(gpu.device, &command_info, &command), "vkAllocateCommandBuffers(present)");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(gpu.Device<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command, &begin), "vkBeginCommandBuffer(present)");
    const auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags stage_from, VkPipelineStageFlags stage_to, VkAccessFlags access_from, VkAccessFlags access_to) {
        VkImageMemoryBarrier entry{}; entry.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; entry.image = image;
        entry.oldLayout = from; entry.newLayout = to; entry.srcAccessMask = access_from; entry.dstAccessMask = access_to;
        entry.srcQueueFamilyIndex = entry.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; entry.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        gpu.Device<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command, stage_from, stage_to, 0, 0, nullptr, 0, nullptr, 1, &entry);
    };
    barrier(source.image, source.initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        source.initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        source.initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {layout.width, layout.height, 1};
    gpu.Device<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(command, upload, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const auto destination = gpu.swapchain_images[index];
    barrier(destination, gpu.swapchain_initialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageBlit blit{}; blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int>(layout.width), static_cast<int>(layout.height), 1};
    blit.dstOffsets[1] = {static_cast<int>(extent.width), static_cast<int>(extent.height), 1};
    gpu.Device<PFN_vkCmdBlitImage>("vkCmdBlitImage")(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    barrier(destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    Check(gpu.Device<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command), "vkEndCommandBuffer(present)");
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &ready;
    Check(gpu.Device<PFN_vkQueueSubmit>("vkQueueSubmit")(gpu.queue, 1, &submit, completed), "vkQueueSubmit(present)"); scope.submitted = true;
    VkSwapchainPresentFenceInfoEXT tracking{}; tracking.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT; tracking.swapchainCount = 1; tracking.pFences = &presented;
    VkPresentInfoKHR present{}; present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; present.pNext = gpu.present_fences ? &tracking : nullptr;
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &ready; present.swapchainCount = 1; present.pSwapchains = &gpu.swapchain; present.pImageIndices = &index;
    scope.queue_pending = !gpu.present_fences;
    const auto status = gpu.Device<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(gpu.queue, &present);
    // OUT_OF_DATE/SUBOPTIMAL still enqueue semaphore waits and the fence.
    if (status == VK_SUCCESS || status == VK_SUBOPTIMAL_KHR || status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_ERROR_SURFACE_LOST_KHR) { if (gpu.present_fences) scope.pending = presented; }
    else { Check(status, "vkQueuePresentKHR"); }
    Check(gpu.Device<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &completed, VK_TRUE, 5'000'000'000ull), "vkWaitForFences(display upload)"); scope.submitted = false;
    source.initialized = true; gpu.swapchain_initialized[index] = true;
    if (gpu.present_fences) {
        Check(gpu.Device<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &presented, VK_TRUE, 5'000'000'000ull), "vkWaitForFences(present completion)"); scope.pending = {};
    } else {
        // This CPU-upload diagnostic path is synchronous already. Normal game
        // rendering uses the asynchronous presenter and per-image semaphores.
        Check(gpu.Device<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(gpu.queue), "vkQueueWaitIdle(compatibility present cleanup)");
        scope.queue_pending = false;
    }
    if (status == VK_ERROR_OUT_OF_DATE_KHR) {
        std::printf("{\"event\":\"native_graphics_present_retry\",\"phase\":\"present\",\"vk_result\":%d,\"swapchain_retired\":true}\n", static_cast<int>(status));
        // The upload and selected presentation cleanup have completed.
        gpu.Device<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(gpu.device, gpu.swapchain, nullptr); gpu.swapchain = {}; return PresentStatus::OutOfDate;
    }
    Check(status == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : status, "vkQueuePresentKHR(completion)");
    if (gpu.validation_errors.load()) { throw std::runtime_error("Vulkan validation reported a presentation error; inspect stderr"); }
    std::printf("{\"event\":\"native_graphics_present_completed\",\"image\":%llu,\"swapchain_index\":%u,\"present_fence_completed\":%s}\n", id, index, gpu.present_fences ? "true" : "false");
    return PresentStatus::Completed;
}
} // namespace kt::gfx
