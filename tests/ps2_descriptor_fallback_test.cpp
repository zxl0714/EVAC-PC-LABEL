#include "context.hpp"
#include "device.hpp"
#include "shader.hpp"
#include "thread_id.hpp"
#include "gs_interface.hpp"
#include "runtime/gs/gs_types.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        const bool push = argc == 2 && std::string_view(argv[1]) == "push";
        const bool native = argc == 2 && std::string_view(argv[1]) == "native";
        Util::register_thread_index(0);
        Require(Vulkan::Context::init_loader(nullptr), "Vulkan loader unavailable");
        Vulkan::Context context;
        context.set_num_thread_indices(1);
        auto flags = Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT |
                     Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT;
        if (native) flags |= Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT;
        Require(context.init_instance_and_device(nullptr, 0, nullptr, 0, flags), "Vulkan device unavailable");

        // Test fixture only: the driver advertises descriptor buffers, while
        // Granite selects descriptor sets (e.g. an unsupported combined layout).
        // Downgrade the host's selected strategy before any allocators exist;
        // the normal runtime still makes its own capability-based choice.
        auto& features = const_cast<Vulkan::DeviceFeatures&>(context.get_enabled_device_features());
        if (!native) {
            Require(features.descriptor_buffer_features.descriptorBuffer, "Fixture requires descriptor buffer support");
            if (push) Require(features.vk14_features.pushDescriptor, "Fixture requires push descriptors");
            features.descriptor_heap_features.descriptorHeap = VK_FALSE;
            features.descriptor_buffer_properties.combinedImageSamplerDescriptorSingleArray = VK_FALSE;
            features.supports_descriptor_buffer = false;
            features.supports_descriptor_buffer_or_heap = false;
            if (!push) features.vk14_features.pushDescriptor = VK_FALSE;
        }

        Vulkan::Device gpu;
        gpu.set_context(context);
        Vulkan::CombinedResourceLayout resources{};
        resources.descriptor_set_mask = 1;
        resources.sets[0].storage_buffer_mask = 1;
        resources.sets[0].meta[0].array_size = 1;
        resources.stages_for_bindings[0][0] = VK_SHADER_STAGE_COMPUTE_BIT;
        resources.stages_for_sets[0] = VK_SHADER_STAGE_COMPUTE_BIT;
        Vulkan::PipelineLayout layout(1, &gpu, resources, nullptr);
        // Stop before a bad template reaches a driver. This fails against the
        // original pinned code, rather than relying on one vendor's crash.
        if (!native) {
            Require(layout.get_update_template(0) != VK_NULL_HANDLE,
                    "Descriptor-set fallback has a NULL update template");
            Require((layout.get_push_set_index() == 0) == push,
                    "Push-descriptor selection disagrees with the chosen strategy");
        }

        auto info = Vulkan::ImageCreateInfo::immutable_2d_image(8, 8, VK_FORMAT_R8G8B8A8_UNORM);
        info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        auto image = gpu.create_image(info);
        Require(bool(image), "Descriptor-set image view creation failed");

        // Exercise the real GS init (including phase LUT creation) and a draw,
        // then verify GPU output. No ROM, window or synthetic input is involved.
        ParallelGS::GSInterface gs;
        Require(gs.init(&gpu, {}), "GS initialization failed");
        auto write = [&](unsigned address, uint64_t value) {
            gs.write_register(static_cast<ParallelGS::RegisterAddr>(address), value);
        };
        write(GS_REG_PRMODECONT, 1);
        write(GS_REG_FRAME_1, uint64_t(1) << 16);
        write(GS_REG_ZBUF_1, uint64_t(1) << 32);
        write(GS_REG_TEST_1, 0x30000);
        write(GS_REG_SCISSOR_1, (uint64_t(63) << 16) | (uint64_t(63) << 48));
        write(GS_REG_XYOFFSET_1, 0);
        write(GS_REG_COLCLAMP, 1);
        write(GS_REG_PRIM, 6 | 0x100);
        constexpr uint32_t color = 0x80563412;
        write(GS_REG_RGBAQ, 0x3f80000000000000ull | color);
        write(GS_REG_XYZ2, 0);
        write(GS_REG_XYZ2, uint64_t(16 * 16) | (uint64_t(16 * 16) << 16));
        std::vector<uint8_t> pixels(4u * 1024u * 1024u);
        std::memcpy(pixels.data(), gs.map_vram_read(0, pixels.size()), pixels.size());
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
            uint32_t actual{};
            std::memcpy(&actual, pixels.data() + GSPSMCT32::addrPSMCT32(0, 1, x, y), 4);
            Require(actual == (x < 16 && y < 16 ? color : 0u), "Fallback GPU pixels differ");
        }
        std::cout << "PASS: advertised=" << features.descriptor_buffer_features.descriptorBuffer
                  << " selected=" << features.supports_descriptor_buffer
                  << " heap=" << features.descriptor_heap_features.descriptorHeap << " push=" << push
                  << "; template, image views, GS init and 4096 GPU pixels verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
