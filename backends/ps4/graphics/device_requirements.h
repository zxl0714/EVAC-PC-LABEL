#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cave::ps4::graphics {
// Vulkan-independent startup policy; use the physical device's version,
// never the loader version. No device creation or GPU work is required.
inline std::string ApiVersionText(std::uint32_t version) {
    return std::to_string((version >> 22) & 0x7fu) + "." +
        std::to_string((version >> 12) & 0x3ffu) + "." +
        std::to_string(version & 0xfffu);
}
inline void RequireVulkanApi(std::uint32_t actual, std::uint32_t required,
                             std::string_view device, std::string_view driver) {
    if ((actual >> 29) == (required >> 29) && actual >= required) return;
    throw std::runtime_error("PS4 graphics requires Vulkan " + ApiVersionText(required) +
        " or newer. Selected GPU: " + std::string(device) + "; driver: " +
        std::string(driver) + "; device Vulkan: " + ApiVersionText(actual) +
        ". Update the graphics driver from the GPU manufacturer's support page and restart Windows.");
}
}
