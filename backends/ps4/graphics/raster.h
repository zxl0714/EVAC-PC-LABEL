#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>

namespace kt::gfx {
enum class BlendFactor : std::uint32_t {
    Zero, One, SourceColor, InverseSourceColor, SourceAlpha, InverseSourceAlpha,
    DestinationAlpha, InverseDestinationAlpha, DestinationColor, InverseDestinationColor,
    SourceAlphaSaturate, ConstantColor, InverseConstantColor, ConstantAlpha, InverseConstantAlpha
};
enum class BlendOperation : std::uint32_t { Add, Subtract, Minimum, Maximum, ReverseSubtract };
struct RasterBlend {
    bool enabled{};
    BlendFactor source_color = BlendFactor::One, destination_color = BlendFactor::Zero;
    BlendFactor source_alpha = BlendFactor::One, destination_alpha = BlendFactor::Zero;
    BlendOperation color{}, alpha{};
    std::array<float, 4> constants{};
};
RasterBlend DecodeGcnBlend(std::uint32_t control);
struct RasterStencil {
    std::uint32_t fail{}, pass{}, depth_fail{}, compare = 7;
    std::uint32_t reference{}, compare_mask = 255, write_mask = 255;
};
RasterStencil DecodeGcnStencil(std::uint32_t operations, std::uint32_t reference_mask, std::uint32_t compare);
struct RasterTexture {
    std::uint32_t binding{}, width{}, height{};
    bool vertex{}, srgb{};
    // AMD component selectors: 0/1 constants, 4/5/6/7 RGBA. Sampled-image
    // swizzle belongs in the Vulkan view; the compiler handles storage images.
    std::array<std::uint32_t, 4> components{4, 5, 6, 7};
    std::span<const std::uint8_t> pixels;
    // Optional immutable decoded snapshot, owning exactly pixels. Keeping a
    // strong reference permits identity reuse without address-reuse hazards.
    // Callers must not retain mutable aliases. Mutable inputs leave this empty.
    std::shared_ptr<const std::vector<std::uint8_t>> snapshot;
};
struct RasterSampler {
    std::uint32_t binding{};
    bool vertex{}, linear_min{}, linear_mag{}, linear_mip{};
    std::array<std::uint32_t, 3> clamp{};
    std::uint32_t border{};
    float bias{}, minimum_lod{}, maximum_lod{};
};
struct RasterBuffer {
    std::uint32_t binding{};
    bool vertex{};
    std::span<const std::uint8_t> bytes;
};
struct RasterDraw {
    std::uint32_t width{}, height{}, vertices{}, instances = 1;
    bool rectangle{}, quad{};
    std::span<const std::uint32_t> vertex, pixel, rectangle_control, rectangle_evaluation;
    std::span<const RasterBuffer> buffers;
    std::span<const RasterTexture> textures;
    std::span<const RasterSampler> samplers;
    // Bytes are raw RGBA attachment storage, not display-channel converted.
    std::span<const std::uint8_t> color;
    std::array<float, 6> viewport{}; // x,y,width,height,minDepth,maxDepth
    std::array<std::int32_t, 4> scissor{}; // x,y,width,height, already clipped
    std::array<std::uint8_t, 56> push_constants{};
    std::uint32_t color_mask = 15;
    RasterBlend blend;
    bool srgb{}, cull_front{}, cull_back{}, clockwise{};
    // Combined host attachment; independent planes preserve guest storage.
    // Clear operations disable draw writes as required by the DB clear mode.
    bool depth{}, stencil{}, clear_depth{}, clear_stencil{};
    RasterStencil stencil_front, stencil_back;
    float depth_clear{};
    std::uint8_t stencil_clear{};
    std::span<const float> depth_data;
    std::span<const std::uint8_t> stencil_data;
    bool depth_write{};
    std::uint32_t depth_compare = 7;
    // Storage reuse alone never proves content. When enabled, completed image
    // contents must byte-match this draw's complete owned/pinned CPU inputs.
    bool reuse_attachment_contents{};
};
struct RasterResult {
    // Depth/stencil are returned only for planes requested by RasterDraw.
    std::vector<std::uint8_t> color, stencil;
    std::vector<float> depth;
    std::uint32_t texture_cache_hits{}, texture_uploads{};
    bool pipeline_cache_hit{};
    bool readback_host_cached{};
    std::uint32_t buffer_cache_hits{}, buffer_creates{};
    std::uint32_t attachment_cache_hits{}, attachment_creates{};
    bool color_content_reused{}, depth_content_reused{}, stencil_content_reused{};
    std::uint64_t color_upload_bytes{}, depth_upload_bytes{}, stencil_upload_bytes{};
    std::uint32_t attachment_snapshot_fallbacks{};
    // Host wall time, not GPU timestamp queries. Phases are disjoint and
    // include exceptional-safe resource retirement on the normal path.
    std::uint64_t resources_us{}, pipeline_us{}, commands_us{}, submit_us{}, wait_us{}, readback_us{}, cleanup_us{};
};
struct RasterCacheStats {
    std::uint64_t texture_hits{}, texture_uploads{}, evictions{}, bytes{};
    std::size_t entries{};
    std::uint64_t pipeline_hits{}, pipeline_creates{}, pipeline_evictions{}, pipeline_key_bytes{};
    std::size_t pipeline_entries{};
    std::uint64_t buffer_hits{}, buffer_creates{}, buffer_evictions{}, buffer_bytes{};
    std::size_t buffer_entries{};
    std::uint64_t attachment_hits{}, attachment_creates{}, attachment_evictions{}, attachment_bytes{};
    std::uint64_t attachment_snapshot_bytes{}, attachment_content_hits{};
    std::uint64_t attachment_snapshot_fallbacks{};
    std::size_t attachment_entries{};
};
} // namespace kt::gfx
