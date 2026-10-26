#pragma once

// Vulkan-independent negotiation, shared by both PS4 graphics paths.
namespace cave::ps4::presentation {
inline constexpr const char* kCapabilities = "VK_KHR_get_surface_capabilities2";
inline constexpr const char* kSurfaceExt = "VK_EXT_surface_maintenance1";
inline constexpr const char* kSurfaceKhr = "VK_KHR_surface_maintenance1";
inline constexpr const char* kSwapchainExt = "VK_EXT_swapchain_maintenance1";
inline constexpr const char* kSwapchainKhr = "VK_KHR_swapchain_maintenance1";

struct Surfaces {
    bool ext{}, khr{};
    constexpr bool Supported() const { return ext || khr; }
};

template<class Has>
Surfaces SurfaceExtensions(Has has) {
    if (!has(kCapabilities)) return {};
    return {has(kSurfaceExt), has(kSurfaceKhr)};
}

template<class Has>
const char* SwapchainExtension(Surfaces enabled, Has has, bool feature = true) {
    if (!feature) return nullptr;
    // Keep the existing EXT path when available. KHR device support requires
    // the KHR instance extension, even if EXT was also advertised.
    if (enabled.ext && has(kSwapchainExt)) return kSwapchainExt;
    if (enabled.khr && has(kSwapchainKhr)) return kSwapchainKhr;
    return nullptr;
}
}
