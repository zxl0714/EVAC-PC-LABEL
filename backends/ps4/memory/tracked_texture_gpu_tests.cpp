#include "tracked_texture.h"
#include "direct_fixture.h"
#include "../graphics/vulkan_device.h"
#include "../graphics/test_programs.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kt::memory {
bool TrackedTextureGpuSelfTest(kt::gfx::VulkanDevice& device) {
    using namespace kt::gfx;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::printf("{\"event\":\"tracked_texture_gpu_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    DirectFixture direct;
    TextureLayout layout;
    layout.address = reinterpret_cast<std::uint64_t>(direct.first); layout.bytes = 32768;
    layout.width = 16; layout.height = 8; layout.pitch = 128; layout.padded_height = 64;
    layout.tiling = 14; layout.components = {4, 5, 6, 7};
    auto* alias = static_cast<std::uint8_t*>(direct.alias);
    std::memset(alias, 73, static_cast<std::size_t>(layout.bytes));
    std::vector<std::uint8_t> initial(layout.width * layout.height * 4);
    for (std::size_t i = 0; i < initial.size(); i += 4) { initial[i + 2] = initial[i + 3] = 255; }
    WriteTexturePixels(layout, initial, {alias, static_cast<std::size_t>(layout.bytes)});
    const auto vertex = AuthoredVertexProgram(), sample = AuthoredSamplePixelProgram(0.5f, 0.5f);
    const auto solid = AuthoredSolidPixelProgram();
    RasterDraw draw;
    draw.width = layout.width; draw.height = layout.height; draw.vertices = 3; draw.rectangle = true;
    draw.vertex = vertex; draw.pixel = sample; draw.rectangle_control = solid.rectangle_control; draw.rectangle_evaluation = solid.rectangle_evaluation;
    draw.color = initial; draw.viewport = {0, 0, 16, 8, 0, 1}; draw.scissor = {0, 0, 16, 8};
    draw.reuse_attachment_contents = true;
    RasterSampler sampler; sampler.binding = 1; sampler.clamp = {2, 2, 2};
    draw.samplers = {&sampler, 1};
    const auto render = [&](const TextureLayout& source, const std::shared_ptr<const std::vector<std::uint8_t>>& pixels) {
        RasterTexture texture;
        texture.width = source.width; texture.height = source.height; texture.pixels = *pixels; texture.snapshot = pixels;
        auto pass = draw; pass.textures = {&texture, 1}; return device.Draw(pass);
    };
    const auto center = (4 * 16 + 8) * 4;
    TrackedTextureCache cache;
    const auto original = cache.Decode(layout);
    const auto first = render(layout, original);
    const auto again = cache.Decode(layout);
    const auto second = render(layout, again);
    check("tracked_native_snapshot_upload_and_generation_hit_produce_identical_vulkan_pixels", first.color[center] == 0 && first.color[center + 2] == 255 &&
        again == original && second.color == first.color && second.texture_cache_hits == 1 && second.texture_uploads == 0 && cache.Stats().generation_hits == 1);
    const auto center_byte = TextureByteOffset(layout, 8, 4);
    alias[center_byte] = 255; alias[center_byte + 2] = 0;
    const auto updated = cache.Decode(layout);
    const auto changed = render(layout, updated);
    check("cpu_alias_change_redecodes_reuploads_and_changes_actual_sampled_texel", changed.texture_uploads == 1 && changed.color[center] == 255 &&
        changed.color[center + 2] == 0 && updated != original && (*original)[center + 2] == 255);
    const std::array<std::uint8_t, 4> blue{0, 0, 255, 255};
    const bool restored = CopyToNativeMemory(alias + center_byte, blue.data(), 4);
    const auto before_offscreen = cache.Decode(layout);
    auto offscreen = draw; offscreen.pixel = solid.spirv; offscreen.samplers = {};
    const auto raster = device.Draw(offscreen);
    WriteTexturePixels(layout, raster.color, {alias, static_cast<std::size_t>(layout.bytes)});
    const auto written_back = cache.Decode(layout);
    const auto sampled = render(layout, written_back);
    check("actual_offscreen_raster_tiled_alias_writeback_invalidates_generation_and_second_pass_pixels", restored &&
        raster.color[center] == 255 && raster.color[center + 2] == 0 && before_offscreen != written_back && (*before_offscreen)[center + 2] == 255 &&
        sampled.color[center] == 255 && sampled.color[center + 2] == 0 && sampled.texture_uploads == 1);
    check("tracked_offscreen_writeback_keeps_guest_row_and_height_padding", alias[TextureByteOffset(layout, layout.width, 0)] == 73 &&
        alias[TextureByteOffset(layout, 0, layout.height)] == 73);
    auto luma = layout;
    luma.bytes = 64; luma.width = luma.height = 4; luma.pitch = luma.padded_height = 8; luma.channels = 1; luma.tiling = 8;
    bool video_ok = true; RasterResult latest;
    for (const auto value : {43u, 71u, 105u}) {
        std::array<unsigned char, 64> frame; frame.fill(static_cast<unsigned char>(value));
        video_ok &= CopyToNativeMemory(alias, frame.data(), frame.size());
        const auto pixels = cache.Decode(luma);
        latest = render(luma, pixels);
        video_ok &= latest.color[center] == value && latest.color[center + 1] == 0 && latest.color[center + 3] == 255 && latest.texture_uploads == 1;
        const auto hits = cache.Stats().generation_hits;
        video_ok &= cache.Decode(luma) == pixels && cache.Stats().generation_hits == hits + 1;
    }
    check("host_video_style_luma_updates_invalidate_cache_and_reach_actual_gpu_sampling", video_ok);
    TrackedTextureCache full(false);
    const auto baseline = render(luma, full.Decode(luma));
    check("full_content_and_tracked_modes_produce_identical_gpu_output_after_host_updates", baseline.color == latest.color &&
        full.Stats().generation_hits == 0 && full.Stats().compared_bytes == luma.bytes && full.Witnesses() == 0);
    device.TrimRasterCaches();
    return passed;
}
}
