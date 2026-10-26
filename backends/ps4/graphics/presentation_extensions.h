#pragma once
#include <cstdlib>
#include <string_view>

// Vulkan-independent negotiation, shared by both PS4 graphics paths.
namespace cave::ps4::presentation {
// Process-local diagnostic switch: exercise the fallback without changing the
// player's configuration or requiring a second GPU/driver installation.
inline bool DiagnosticCompatibility() {
#ifdef _WIN32
    char* value{};
    std::size_t length{};
    if (_dupenv_s(&value, &length, "EVAC_PS4_PRESENT_COMPAT") != 0) return false;
    const bool force = value && std::string_view{value} == "1";
    std::free(value);
    return force;
#else
    const auto* value = std::getenv("EVAC_PS4_PRESENT_COMPAT");
    return value && std::string_view{value} == "1";
#endif
}
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
