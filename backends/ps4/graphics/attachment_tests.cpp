#include "vulkan_device.h"
#include "test_programs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kt::gfx {
bool AttachmentSelfTest(VulkanDevice& device) {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::printf("{\"event\":\"attachment_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    const auto rejects = [&](const RasterDraw& draw) { try { (void)device.Draw(draw); return false; } catch (const std::exception&) { return true; } };
    device.TrimRasterCaches();
    const auto vertex = AuthoredVertexProgram(); const auto pixel = AuthoredSolidPixelProgram();
    std::vector<std::uint8_t> color(32 * 16 * 4);
    for (std::size_t i = 0; i < color.size(); i += 4) { color[i + 2] = color[i + 3] = 255; }
    RasterDraw draw;
    draw.width = 32; draw.height = 16; draw.vertices = 3; draw.rectangle = true;
    draw.vertex = vertex; draw.pixel = pixel.spirv; draw.rectangle_control = pixel.rectangle_control; draw.rectangle_evaluation = pixel.rectangle_evaluation;
    draw.color = color; draw.viewport = {0, 0, 32, 16, 0, 1}; draw.scissor = {4, 3, 24, 10}; draw.reuse_attachment_contents = true;
    auto result = device.Draw(draw); const auto first = result;
    check("fresh_attachment_uploads_input_and_captures_only_fence_completed_output", result.color_upload_bytes == color.size() && !result.color_content_reused &&
        result.color[0] == 0 && result.color[(8 * 32 + 16) * 4] == 255 && device.CacheStats().attachment_snapshot_bytes >= color.size());
    color = result.color; draw.color = color;
    result = device.Draw(draw);
    check("identical_completed_color_content_skips_upload_without_changing_pixels", result.color_content_reused && result.color_upload_bytes == 0 && result.color == first.color);
    auto rotated = draw; rotated.width = 16; rotated.height = 32;
    std::vector<std::uint8_t> other_color(color.size(), 29); rotated.color = other_color;
    rotated.viewport = {0, 0, 16, 32, 0, 1}; rotated.scissor = {0, 0, 16, 32};
    (void)device.Draw(rotated); result = device.Draw(draw);
    check("held_readback_proof_is_not_recycled_by_an_unrelated_attachment", result.color_content_reused && result.color == first.color);
    color.back() = 137;
    result = device.Draw(draw);
    check("same_pointer_tail_byte_change_forces_complete_color_upload", !result.color_content_reused && result.color_upload_bytes == color.size() && result.color.back() == 137);
    color = result.color; draw.color = color;
    auto disabled = draw; disabled.reuse_attachment_contents = false;
    result = device.Draw(disabled);
    check("disabled_content_reuse_keeps_full_upload_and_identical_output", !result.color_content_reused && result.color_upload_bytes == color.size() && result.color == color);
    result = device.Draw(draw);
    check("reenabling_after_untracked_draw_requires_new_content_capture", !result.color_content_reused && result.color_upload_bytes == color.size());
    result = device.Draw(draw);
    check("fresh_recapture_allows_later_color_hit", result.color_content_reused && result.color_upload_bytes == 0);
    std::vector<float> depth(512, 0.25f); std::vector<std::uint8_t> stencil(512, 17);
    draw.depth = draw.stencil = true; draw.depth_compare = 7;
    draw.depth_data = depth; draw.stencil_data = stencil;
    auto planes = device.Draw(draw);
    check("new_depth_stencil_attachment_imports_both_exact_input_planes", planes.depth_upload_bytes == depth.size() * 4 && planes.stencil_upload_bytes == stencil.size() &&
        planes.depth == depth && planes.stencil == stencil);
    planes = device.Draw(draw);
    check("identical_depth_and_stencil_planes_reuse_completed_gpu_contents", planes.depth_content_reused && planes.stencil_content_reused &&
        planes.depth_upload_bytes == 0 && planes.stencil_upload_bytes == 0 && planes.depth == depth && planes.stencil == stencil);
    depth.back() = 0.75f; planes = device.Draw(draw);
    check("depth_only_update_uploads_depth_without_discarding_stencil", !planes.depth_content_reused && planes.stencil_content_reused &&
        planes.depth_upload_bytes == depth.size() * 4 && planes.stencil_upload_bytes == 0 && planes.depth == depth && planes.stencil == stencil);
    stencil.back() = 43; planes = device.Draw(draw);
    check("stencil_only_update_preserves_reused_depth_during_partial_upload", planes.depth_content_reused && !planes.stencil_content_reused &&
        planes.depth_upload_bytes == 0 && planes.stencil_upload_bytes == stencil.size() && planes.depth == depth && planes.stencil == stencil);
    draw.clear_depth = true; draw.depth_clear = 0.5f; planes = device.Draw(draw);
    check("depth_clear_does_not_upload_or_destroy_matching_stencil", planes.depth_upload_bytes == 0 && planes.stencil_upload_bytes == 0 && planes.stencil_content_reused &&
        std::all_of(planes.depth.begin(), planes.depth.end(), [](float value) { return value == 0.5f; }) && planes.stencil == stencil);
    draw.clear_depth = false; depth = planes.depth; draw.depth_data = depth;
    planes = device.Draw(draw);
    check("cleared_depth_becomes_reusable_only_as_exact_next_input", planes.depth_content_reused && planes.stencil_content_reused && planes.depth == depth && planes.stencil == stencil);
    draw.depth_write = true; std::fill(depth.begin(), depth.end(), 0.75f);
    const auto raw_depth = device.Draw(draw);
    depth = raw_depth.depth;
    for (auto& value : depth) { value = std::round(value * 65535) / 65535; }
    draw.depth_data = depth; draw.depth_write = false;
    const auto quantized = device.Draw(draw);
    check("z16_quantization_difference_forces_depth_upload_instead_of_reusing_raw_d32", raw_depth.depth[8 * 32 + 16] == 0.5f && depth[8 * 32 + 16] != 0.5f &&
        !quantized.depth_content_reused && quantized.depth_upload_bytes == depth.size() * 4 && quantized.depth == depth);
    auto invalid = draw; RasterTexture bad_texture;
    invalid.textures = {&bad_texture, 1};
    const bool refused = rejects(invalid); const auto recovered = device.Draw(draw);
    check("exception_after_acquiring_attachments_invalidates_reuse_until_new_fence", refused && !recovered.color_content_reused &&
        !recovered.depth_content_reused && recovered.color_upload_bytes == color.size() && recovered.depth == depth && recovered.stencil == stencil);
    auto different_format = draw; different_format.srgb = true;
    const auto srgb = device.Draw(different_format);
    check("format_change_never_reuses_unorm_color_as_srgb", !srgb.color_content_reused && srgb.color_upload_bytes == color.size());
    const auto before_budget = device.CacheStats().attachment_evictions;
    auto large = draw; large.depth = large.stencil = large.depth_write = false; large.depth_data = {}; large.stencil_data = {};
    std::vector<std::uint8_t> large_color;
    for (unsigned i = 0; i < 8; ++i) {
        large.width = 1024 + 64 * i; large.height = 1024;
        large_color.assign(std::size_t{large.width} * large.height * 4, 0); large.color = large_color;
        large.viewport = {0, 0, static_cast<float>(large.width), static_cast<float>(large.height), 0, 1};
        large.scissor = {0, 0, static_cast<int>(large.width), static_cast<int>(large.height)};
        (void)device.Draw(large);
    }
    const auto bounded = device.CacheStats();
    check("attachment_gpu_and_cpu_snapshot_storage_share_one_bounded_budget", bounded.attachment_entries <= 8 && bounded.attachment_snapshot_bytes > 0 &&
        bounded.attachment_bytes + bounded.attachment_snapshot_bytes <= 128ull * 1024 * 1024 && bounded.attachment_evictions > before_budget);
    device.TrimRasterCaches(); const auto empty = device.CacheStats();
    check("cache_trim_retires_snapshots_together_with_gpu_images", empty.attachment_bytes == 0 && empty.attachment_snapshot_bytes == 0 && empty.attachment_entries == 0);
    std::printf("{\"event\":\"attachment_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
}
