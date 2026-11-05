#include "device.hpp"
#include "shaders/slangmosh_iface.hpp"
#include "shaders/slangmosh.hpp"

namespace cave::ps2 {
// Reuse the pinned renderer's checked-in fullscreen-triangle / texture shaders.
Vulkan::Program* CreateGsPresentProgram(Vulkan::Device& device) {
    Vulkan::ResourceLayout layout;
    const ParallelGS::Shaders<> shaders(device,layout,0);
    return device.request_program(shaders.quad,shaders.blit_circuit);
}
Vulkan::Program* CreateGsFieldProgram(Vulkan::Device& device) {
    Vulkan::ResourceLayout layout;
    const ParallelGS::Shaders<> shaders(device,layout,0);
    return device.request_program(shaders.quad,shaders.weave);
}
}
