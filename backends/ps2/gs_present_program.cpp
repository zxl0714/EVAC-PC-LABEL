#include "device.hpp"
#include "shaders/slangmosh_iface.hpp"
#include "shaders/slangmosh.hpp"

namespace cave::ps2 {
#include "gs_present_rotate_spv.h"

// Reuse the pinned renderer's checked-in fullscreen-triangle / texture shaders.
Vulkan::Program* CreateGsPresentProgram(Vulkan::Device& device,bool rotate) {
    Vulkan::ResourceLayout layout;
    const ParallelGS::Shaders<> shaders(device,layout,0);
    auto* vertex=shaders.quad;
    if(rotate) {
        Vulkan::ResourceLayout rotationLayout{};
        rotationLayout.output_mask=1; // UV at location zero.
        vertex=device.request_shader(GsPresentRotateSpv,sizeof(GsPresentRotateSpv),&rotationLayout);
    }
    return device.request_program(vertex,shaders.blit_circuit);
}
Vulkan::Program* CreateGsFieldProgram(Vulkan::Device& device) {
    Vulkan::ResourceLayout layout;
    const ParallelGS::Shaders<> shaders(device,layout,0);
    return device.request_program(shaders.quad,shaders.weave);
}
}
