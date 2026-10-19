#include "native_gpu.h"
#include "native_memory.h"
#include "native_resident_gpu.h"
#include "graphics/vulkan_device.h"
#include "graphics/shader_binary.h"
#include "graphics/texture.h"
#include "memory/tracked_texture.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <atomic>
#include <stdexcept>

namespace {
bool texture_tracking = true;
bool attachment_reuse = true;
using kt::gfx::pm4::RegisterBank;
unsigned Word(const RegisterBank& bank, unsigned index) {
    const auto value = bank.Get(index);
    if (!value) { throw std::runtime_error("Raster state reads unknown register " + std::to_string(index)); }
    return *value;
}
struct Pin {
    void* address{};
    std::uint64_t physical{}, bytes{};
    std::uint64_t allocation_identity{};
    unsigned protection{};
    Pin(std::uint64_t value, std::uint64_t length, unsigned required = 0x33) : bytes(length), protection(required) {
        NativeMemoryRangeInfo info;
        if (!value || !length || !RetainNativeMemoryRangeInfo(reinterpret_cast<void*>(value), length, &info, required)) {
            throw std::runtime_error("Raster resource is not live memory with the required permissions");
        }
        address = reinterpret_cast<void*>(value);
        physical = info.physical;
        allocation_identity = info.allocation_identity;
    }
    ~Pin() { ReleaseNativeMemoryRange(address, protection); }
};
// Single-sample Depth2DThin64, base machine, no array slices/bank XOR.
// 256x128 macrotiles; depth samples >64 bytes split into separate tile slices.
std::uint64_t DepthOffset(unsigned x, unsigned y, unsigned pitch, unsigned height, unsigned bytes) {
    if ((bytes != 1 && bytes != 2) || pitch % 256 || height % 128 || x >= pitch || y >= height) {
        throw std::runtime_error("Unsupported depth tiled extent");
    }
    const auto bit = [](unsigned v, unsigned b) { return (v >> b) & 1u; };
    const auto micro = bit(x, 0) | (bit(y, 0) << 1) | (bit(x, 1) << 2) | (bit(y, 1) << 3) | (bit(x, 2) << 4) | (bit(y, 2) << 5);
    const auto split = micro * bytes / 64;
    const auto pipe = (bit(x, 3) ^ bit(y, 3) ^ bit(x, 4)) | ((bit(x, 4) ^ bit(y, 4)) << 1) | ((bit(x, 5) ^ bit(y, 5)) << 2);
    const auto tx = x / 64, ty = y / 32;
    const auto bank = ((bit(tx, 0) ^ bit(ty, 3)) | ((bit(tx, 1) ^ bit(ty, 2) ^ bit(ty, 3)) << 1) |
        ((bit(tx, 2) ^ bit(ty, 1)) << 2) | ((bit(tx, 3) ^ bit(ty, 0)) << 3)) ^ (9 * split);
    const std::uint64_t macro = std::uint64_t{y / 128} * (pitch / 256) + x / 256;
    const auto total = std::uint64_t{pitch / 256} * (height / 128) * 256 * split + macro * 256 +
        ((y / 8) % 4) * 64 + (micro * bytes % 64);
    return (total & 255) | (std::uint64_t{pipe} << 8) | (std::uint64_t{bank & 15} << 11) | ((total >> 8) << 15);
}
std::span<const std::uint32_t> DepthOffsets(unsigned pitch, unsigned height, unsigned width, unsigned visible_height, unsigned bytes) {
    struct Lookup { unsigned pitch{}, height{}, width{}, visible_height{}; std::vector<std::uint32_t> offsets; };
    thread_local std::array<Lookup, 2> lookups;
    if (bytes != 1 && bytes != 2) { throw std::runtime_error("Invalid depth lookup element size"); }
    auto& lookup = lookups[bytes - 1];
    if (lookup.pitch != pitch || lookup.height != height || lookup.width != width || lookup.visible_height != visible_height) {
        std::vector<std::uint32_t> offsets(std::size_t{width} * visible_height);
        for (unsigned y = 0; y < visible_height; ++y) { for (unsigned x = 0; x < width; ++x) {
            const auto offset = DepthOffset(x, y, pitch, height, bytes);
            if (offset > UINT32_MAX || offset + bytes > std::uint64_t{pitch} * height * bytes) { throw std::runtime_error("Depth lookup exceeds backing"); }
            offsets[std::size_t{y} * width + x] = static_cast<std::uint32_t>(offset);
        } }
        lookup = {pitch, height, width, visible_height, std::move(offsets)};
    }
    return lookup.offsets;
}
bool PlainFlipReservation(const kt::gfx::pm4::Packet& packet, bool already_prepared, bool final_stream, bool at_end) {
    // This SDK reservation is an opaque 64-DWORD NOP. Only its header and
    // first marker are specified; reused DCB storage may retain old commands
    // in the remaining payload. They are neither executed nor followed.
    return !already_prepared && final_stream && at_end && packet.header == 0xc03e1000 &&
        packet.body.size() == 63 && packet.body[0] == 0x68750777;
}
kt::gfx::TextureLayout ColorAttachment(const kt::gfx::pm4::RegisterBank& c, const NativeVideoGpuContext& display) {
    const auto base = std::uint64_t{Word(c, 0x318)} << 8;
    const auto pitch_word = Word(c, 0x319), slice_word = Word(c, 0x31a), attrib = Word(c, 0x31d);
    const auto pitch = ((pitch_word & 2047) + 1) * 8;
    const auto slice = std::uint64_t{(slice_word & 0x3fffff) + 1} * 64;
    const auto tiling = attrib & 31;
    // Single-sample RGBA8 UNORM with the export swizzle consumed by the
    // shader compiler. FMASK tile index is inert at one sample; validate its
    // known encoding rather than accepting MSAA/compression/array fields.
    if (!base || base % 32768 || pitch_word & ~0x7ff007ffu || slice_word & ~0x3fffffu ||
        Word(c, 0x31b) || Word(c, 0x31c) != 0x8828 ||
        (attrib != 0x14a && attrib != 0x1ce) || Word(c, 0x202) != 0xcc0010 ||
        pitch > 8192 || pitch % 128 || slice % pitch || (slice / pitch) % 64 || slice / pitch > 8192 || slice * 4 > 64 * 1024 * 1024) {
        throw std::runtime_error("Unsupported color attachment layout/format/control");
    }
    kt::gfx::TextureLayout result;
    result.address = base; result.pitch = result.width = pitch;
    result.padded_height = result.height = static_cast<unsigned>(slice / pitch);
    result.bytes = slice * 4; result.tiling = tiling; result.components = {4, 5, 6, 7};
    // Only a matching registered display gives a logical crop. Offscreen
    // extent comes from CB_PITCH/SLICE, never from the unrelated video output.
    if (base == display.address && tiling == 10 && display.layout.tiled &&
        display.layout.pitch == pitch && display.layout.bytes == result.bytes) {
        result.width = display.layout.width; result.height = display.layout.height;
    }
    return result;
}
void ExecuteDraw(NativeVideoGpuContext& target, const kt::gfx::pm4::Analysis& state, kt::gfx::ShaderMemoryReader reader) {
    using namespace kt::gfx;
    const auto started = std::chrono::steady_clock::now();
    const auto& c = state.context;
    TextureLayout attachment;
    try { attachment = ColorAttachment(c, target); }
    catch (...) {
        std::printf("{\"event\":\"native_gpu_attachment_frontier\",\"base\":\"0x%llx\",\"display_base\":\"0x%llx\",\"pitch\":%u,\"display_pitch\":%u,\"slice_pixels\":%llu,\"display_pixels\":%llu,\"view\":%u,\"info\":%u,\"attrib\":%u,\"blend\":%u,\"color_control\":%u}\n",
            std::uint64_t{Word(c, 0x318)} << 8, target.address, ((Word(c, 0x319) & 2047) + 1) * 8,
            target.layout.pitch, std::uint64_t{(Word(c, 0x31a) & 0x3fffff) + 1} * 64, target.layout.bytes / 4,
            Word(c, 0x31b), Word(c, 0x31c), Word(c, 0x31d), Word(c, 0x1e0), Word(c, 0x202));
        throw;
    }
    Pin color_pin(attachment.address, attachment.bytes);
    const std::span<std::uint8_t> color_backing{static_cast<std::uint8_t*>(color_pin.address), static_cast<std::size_t>(color_pin.bytes)};
    const bool display_attachment = attachment.address == target.address && attachment.tiling == 10 &&
        attachment.width == target.layout.width && attachment.height == target.layout.height &&
        attachment.pitch == target.layout.pitch && attachment.bytes == target.layout.bytes && target.layout.tiled;
    RasterDraw draw{}; draw.width = attachment.width; draw.height = attachment.height; draw.reuse_attachment_contents = attachment_reuse;
    draw.blend = DecodeGcnBlend(Word(c, 0x1e0));
    const auto constant_factor = [](BlendFactor value) { return value >= BlendFactor::ConstantColor; };
    if (draw.blend.enabled && (constant_factor(draw.blend.source_color) || constant_factor(draw.blend.destination_color) ||
        constant_factor(draw.blend.source_alpha) || constant_factor(draw.blend.destination_alpha))) {
        for (unsigned index = 0; index < 4; ++index) { draw.blend.constants[index] = std::bit_cast<float>(Word(c, 0x105 + index)); }
    }
    const auto primitive = Word(state.uconfig, 0x242);
    if (primitive != 17 && primitive != 4 && primitive != 19) { throw std::runtime_error("Unimplemented primitive topology " + std::to_string(primitive)); }
    draw.rectangle = primitive == 17;
    draw.quad = primitive == 19;
    if (!state.last_draw_vertices || !state.instances) { throw std::runtime_error("Missing draw/instance count"); }
    draw.vertices = *state.last_draw_vertices; draw.instances = *state.instances;
    if (Word(c, 0x206) != 0x43f) { throw std::runtime_error("Unimplemented viewport transform control"); }
    std::array<float, 6> bounds{};
    for (unsigned index = 0; index < 6; ++index) { bounds[index] = std::bit_cast<float>(Word(c, 0x10f + index)); }
    draw.viewport = {bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[0] * 2, bounds[2] * 2,
        bounds[5] - bounds[4], bounds[5] + bounds[4]};
    const auto scissor0 = Word(c, 0xc), scissor1 = Word(c, 0xd);
    const auto left = scissor0 & 32767, top = (scissor0 >> 16) & 32767;
    const auto right = std::min(scissor1 & 32767, draw.width), bottom = std::min((scissor1 >> 16) & 32767, draw.height);
    if (left > right || top > bottom) { throw std::runtime_error("Invalid screen scissor"); }
    draw.scissor = {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left), static_cast<int>(bottom - top)};
    const auto depth_control = Word(c, 0x200), depth_render = Word(c, 0);
    if (depth_render & ~3u || depth_control & ~0x7007f7u) { throw std::runtime_error("Unimplemented depth/stencil mode"); }
    draw.depth = (depth_control & 2) != 0; draw.stencil = (depth_control & 1) != 0;
    draw.depth_write = (depth_control & 4) != 0; draw.depth_compare = (depth_control >> 4) & 7;
    draw.clear_depth = (depth_render & 1) != 0; draw.clear_stencil = (depth_render & 2) != 0;
    if (draw.clear_depth && (!draw.depth || !draw.depth_write || draw.depth_compare != 7)) { throw std::runtime_error("Unsupported partial depth clear"); }
    if (draw.stencil && !draw.clear_stencil) {
        const auto operations = Word(c, 0x10b);
        if (operations & 0xff000000) { throw std::runtime_error("Reserved stencil control bits"); }
        draw.stencil_front = DecodeGcnStencil(operations & 0xfff, Word(c, 0x10c), (depth_control >> 8) & 7);
        draw.stencil_back = (depth_control & 128) ? DecodeGcnStencil((operations >> 12) & 0xfff, Word(c, 0x10d), (depth_control >> 20) & 7) : draw.stencil_front;
    }
    draw.depth_clear = std::bit_cast<float>(Word(c, 0xb)); draw.stencil_clear = static_cast<std::uint8_t>(Word(c, 0xa));
    if (!std::isfinite(draw.depth_clear) || draw.depth_clear < 0 || draw.depth_clear > 1 || Word(c, 0xa) > 255) { throw std::runtime_error("Invalid depth/stencil clear value"); }
    const auto polygon = Word(c, 0x205);
    const auto polygon_mode = (polygon >> 3) & 3;
    // Front/back mode fields are ignored while POLY_MODE is disabled. Games
    // still commonly write FILL (2) to both, e.g. 0x240.
    if ((polygon & ~0x807ffu) || (polygon_mode && (polygon_mode != 1 || ((polygon >> 5) & 7) != 2 || ((polygon >> 8) & 7) != 2))) {
        throw std::runtime_error("Unimplemented polygon mode/bias");
    }
    draw.cull_front = (polygon & 1) != 0; draw.cull_back = (polygon & 2) != 0; draw.clockwise = (polygon & 4) != 0;
    const auto mask = Word(c, 0x8e) & Word(c, 0x8f);
    if (mask & ~15u) { throw std::runtime_error("Multiple color attachments are not implemented"); } draw.color_mask = mask;
    const auto state_ready = std::chrono::steady_clock::now();
    thread_local ShaderCache shader_cache;
    thread_local kt::memory::TrackedTextureCache texture_cache{texture_tracking};
    const auto texture_before = texture_cache.Stats();
    const auto shader_hits_before = shader_cache.Stats().hits;
    const auto compile = [&](ShaderStage stage, unsigned low, unsigned binding) {
        const auto address = (std::uint64_t{Word(state.shader, low)} << 8) | (std::uint64_t{Word(state.shader, low + 1)} << 40);
        const auto code = ReadShaderBinary(reader, nullptr, address);
        if (code.stage != stage) { throw std::runtime_error("Bound shader stage mismatch"); }
        return shader_cache.Get(stage, code.instructions, state.shader, c, reader, nullptr, binding, address);
    };
    const auto vs = compile(ShaderStage::Vertex, 0x48, 0);
    const auto ps = compile(ShaderStage::Pixel, 8, vs->buffer_bindings + vs->image_bindings + vs->sampler_bindings);
    draw.vertex = vs->spirv; draw.pixel = ps->spirv; draw.rectangle_control = draw.quad ? ps->quad_control : ps->rectangle_control; draw.rectangle_evaluation = ps->rectangle_evaluation;
    const auto shaders_ready = std::chrono::steady_clock::now();
    std::vector<std::vector<std::uint8_t>> buffer_data;
    std::vector<RasterBuffer> buffers;
    buffer_data.reserve(vs->buffers.size() + ps->buffers.size()); buffers.reserve(buffer_data.capacity());
    const auto add_buffers = [&](const CompiledShader& shader, bool vertex) {
        for (const auto& entry : shader.buffers) {
            if (entry.writable || !entry.bytes || entry.bytes > 64 * 1024 * 1024) { throw std::runtime_error("Writable/oversized shader buffer execution pending"); }
            auto& bytes = buffer_data.emplace_back(static_cast<std::size_t>(entry.bytes));
            if (entry.flat_constants) { std::memcpy(bytes.data(), shader.flat_constants.data(), bytes.size()); }
            else if (!reader(nullptr, entry.address, bytes.data(), bytes.size())) { throw std::runtime_error("Shader buffer snapshot failed"); }
            buffers.push_back({entry.binding, vertex, bytes});
        }
    };
    add_buffers(*vs, true); add_buffers(*ps, false); draw.buffers = buffers;
    const auto buffers_ready = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> texture_data;
    std::vector<RasterTexture> textures;
    std::vector<RasterSampler> samplers;
    texture_data.reserve(vs->images.size() + ps->images.size()); textures.reserve(texture_data.capacity());
    const auto add_images = [&](const CompiledShader& shader, bool vertex) {
        for (const auto& entry : shader.images) {
            if (entry.unsupported_access) { throw std::runtime_error("Storage/depth/array texture access execution pending"); }
            TextureLayout layout;
            try { layout = DescribeTexture(entry.descriptor); }
            catch (...) {
                const auto& words = entry.descriptor;
                std::printf("{\"event\":\"native_gpu_texture_frontier\",\"descriptor\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",
                    words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
                throw;
            }
            // The native cache pins current memory before checking a bounded
            // weak witness; invalid/missing proofs keep full-content checking.
            // Graphics receives only immutable, owned pixel snapshots.
            const auto& pixels = texture_data.emplace_back(texture_cache.Decode(layout));
            textures.push_back({entry.binding, layout.width, layout.height, vertex, layout.srgb, layout.components, *pixels, pixels});
        }
        for (const auto& entry : shader.samplers) {
            auto sampler = DescribeSampler(entry.descriptor); sampler.binding = entry.binding; sampler.vertex = vertex; samplers.push_back(sampler);
        }
    };
    add_images(*vs, true); add_images(*ps, false); draw.textures = textures; draw.samplers = samplers;
    const auto textures_ready = std::chrono::steady_clock::now();
    auto color = display_attachment ? ReadDisplayPixels(target.layout, color_backing) : ReadTexturePixels(attachment, color_backing);
    draw.color = color;
    const auto color_ready = std::chrono::steady_clock::now();
    std::unique_ptr<Pin> depth_pin, stencil_pin;
    std::vector<float> depth_data;
    std::vector<std::uint8_t> stencil_data;
    std::span<const std::uint32_t> depth_offsets, stencil_offsets;
    unsigned depth_pitch{}, depth_height{};
    if (draw.depth || draw.stencil) {
        const auto zinfo = Word(c, 0x10), sinfo = Word(c, 0x11), size = Word(c, 0x16);
        depth_pitch = ((size & 2047) + 1) * 8; depth_height = (((size >> 11) & 2047) + 1) * 8;
        if (zinfo != 0x80000001 || sinfo != 0x20000001 || Word(c, 0x12) != Word(c, 0x14) || Word(c, 0x13) != Word(c, 0x15) ||
            depth_pitch < draw.width || depth_height < draw.height || depth_pitch % 256 || depth_height % 128 ||
            std::uint64_t{depth_pitch} * depth_height != std::uint64_t{(Word(c, 0x17) & 0x3fffff) + 1} * 64) {
            throw std::runtime_error("Depth/stencil surface layout execution pending");
        }
        if (draw.depth) { depth_pin = std::make_unique<Pin>(std::uint64_t{Word(c, 0x14)} << 8, std::uint64_t{depth_pitch} * depth_height * 2); }
        if (draw.stencil) { stencil_pin = std::make_unique<Pin>(std::uint64_t{Word(c, 0x15)} << 8, std::uint64_t{depth_pitch} * depth_height); }
        const auto overlaps = [](std::uint64_t a, std::uint64_t an, std::uint64_t b, std::uint64_t bn) { return a < b + bn && b < a + an; };
        if ((depth_pin && overlaps(color_pin.physical, color_pin.bytes, depth_pin->physical, depth_pin->bytes)) ||
            (stencil_pin && overlaps(color_pin.physical, color_pin.bytes, stencil_pin->physical, stencil_pin->bytes)) ||
            (depth_pin && stencil_pin && overlaps(depth_pin->physical, depth_pin->bytes, stencil_pin->physical, stencil_pin->bytes))) {
            throw std::runtime_error("Aliasing depth/stencil/color attachments are not supported");
        }
        const auto pixels = std::size_t{draw.width} * draw.height;
        if (depth_pin) { depth_offsets = DepthOffsets(depth_pitch, depth_height, draw.width, draw.height, 2); }
        if (stencil_pin) { stencil_offsets = DepthOffsets(depth_pitch, depth_height, draw.width, draw.height, 1); }
        if (depth_pin && !draw.clear_depth) {
            depth_data.resize(pixels);
            for (unsigned y = 0; y < draw.height; ++y) { for (unsigned x = 0; x < draw.width; ++x) {
                std::uint16_t value{}; std::memcpy(&value, static_cast<const std::uint8_t*>(depth_pin->address) + depth_offsets[std::size_t{y} * draw.width + x], 2);
                depth_data[std::size_t{y} * draw.width + x] = static_cast<float>(value) / 65535;
            } }
            draw.depth_data = depth_data;
        }
        if (stencil_pin && !draw.clear_stencil) {
            stencil_data.resize(pixels);
            for (unsigned y = 0; y < draw.height; ++y) { for (unsigned x = 0; x < draw.width; ++x) {
                stencil_data[std::size_t{y} * draw.width + x] = static_cast<const std::uint8_t*>(stencil_pin->address)[stencil_offsets[std::size_t{y} * draw.width + x]];
            } }
            draw.stencil_data = stencil_data;
        }
    }
    const auto prepared = std::chrono::steady_clock::now();
    const auto result = target.device->Draw(draw);
    const auto rasterized = std::chrono::steady_clock::now();
    if (display_attachment) { WriteDisplayPixels(target.layout, result.color, color_backing); }
    else { WriteTexturePixels(attachment, result.color, color_backing); }
    const auto color_written = std::chrono::steady_clock::now();
    for (unsigned y = 0; y < draw.height && (depth_pin || stencil_pin); ++y) {
        for (unsigned x = 0; x < draw.width; ++x) {
            const auto pixel = std::size_t{y} * draw.width + x;
            if (depth_pin) {
                const auto depth = static_cast<std::uint16_t>(std::round(std::clamp(result.depth[pixel], 0.0f, 1.0f) * 65535));
                std::memcpy(static_cast<std::uint8_t*>(depth_pin->address) + depth_offsets[pixel], &depth, 2);
            }
            if (stencil_pin) { static_cast<std::uint8_t*>(stencil_pin->address)[stencil_offsets[pixel]] = result.stencil[pixel]; }
        }
    }
    const auto depth_written = std::chrono::steady_clock::now();
    const auto nonzero = std::count_if(result.color.begin(), result.color.end(), [](auto value) { return value != 0; });
    const auto finished = std::chrono::steady_clock::now();
    static std::atomic<unsigned> offscreen_traces{};
    if (!display_attachment && offscreen_traces.fetch_add(1) < 16) {
        std::printf("{\"event\":\"native_gpu_offscreen_writeback\",\"address\":\"0x%llx\",\"width\":%u,\"height\":%u,\"pitch\":%u,\"tile_mode\":%u,\"bytes\":%llu,\"gpu_fence_completed\":true,\"presented\":false}\n",
            attachment.address, attachment.width, attachment.height, attachment.pitch, attachment.tiling, attachment.bytes);
    }
    const auto micros = [](auto begin, auto end) { return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count(); };
    const auto texture_after = texture_cache.Stats();
    std::printf("{\"event\":\"native_gpu_draw_completed\",\"vertices\":%u,\"rectangle\":%s,\"quad\":%s,\"width\":%u,\"height\":%u,\"color_nonzero_bytes\":%zu,\"prepare_us\":%lld,\"raster_us\":%lld,\"writeback_us\":%lld,\"state_us\":%lld,\"shaders_us\":%lld,\"buffers_us\":%lld,\"textures_us\":%lld,\"color_import_us\":%lld,\"depth_import_us\":%lld,\"color_export_us\":%lld,\"depth_export_us\":%lld,\"diagnostics_us\":%lld,\"texture_read_bytes\":%llu,\"texture_snapshot_bytes\":%llu,\"texture_generation_hits\":%llu,\"shader_cache_hits\":%llu,\"texture_decode_hits\":%llu,\"gpu_texture_hits\":%u,\"texture_uploads\":%u,\"gpu_pipeline_hit\":%s,\"gpu_fence_completed\":true,\"guest_writeback\":true,\"presented\":false}\n",
        draw.vertices, draw.rectangle ? "true" : "false", draw.quad ? "true" : "false", draw.width, draw.height, static_cast<std::size_t>(nonzero),
        micros(started, prepared), micros(prepared, rasterized), micros(rasterized, finished),
        micros(started, state_ready), micros(state_ready, shaders_ready), micros(shaders_ready, buffers_ready), micros(buffers_ready, textures_ready),
        micros(textures_ready, color_ready), micros(color_ready, prepared), micros(rasterized, color_written), micros(color_written, depth_written), micros(depth_written, finished),
        texture_after.compared_bytes - texture_before.compared_bytes, texture_after.snapshot_bytes - texture_before.snapshot_bytes,
        texture_after.generation_hits - texture_before.generation_hits,
        shader_cache.Stats().hits - shader_hits_before, texture_after.content_hits - texture_before.content_hits,
        result.texture_cache_hits, result.texture_uploads, result.pipeline_cache_hit ? "true" : "false");
    std::printf("{\"event\":\"native_gpu_raster_phases\",\"resources_us\":%llu,\"pipeline_us\":%llu,\"commands_us\":%llu,\"submit_us\":%llu,\"wait_us\":%llu,\"readback_us\":%llu,\"cleanup_us\":%llu,\"readback_host_cached\":%s,\"buffer_cache_hits\":%u,\"buffer_creates\":%u,\"attachment_cache_hits\":%u,\"attachment_creates\":%u,\"color_content_reused\":%s,\"depth_content_reused\":%s,\"stencil_content_reused\":%s,\"color_upload_bytes\":%llu,\"depth_upload_bytes\":%llu,\"stencil_upload_bytes\":%llu,\"attachment_snapshot_fallbacks\":%u,\"depth_readback_bytes\":%zu,\"stencil_readback_bytes\":%zu}\n",
        result.resources_us, result.pipeline_us, result.commands_us, result.submit_us, result.wait_us, result.readback_us, result.cleanup_us,
        result.readback_host_cached ? "true" : "false", result.buffer_cache_hits, result.buffer_creates,
        result.attachment_cache_hits, result.attachment_creates, result.color_content_reused ? "true" : "false", result.depth_content_reused ? "true" : "false",
        result.stencil_content_reused ? "true" : "false", result.color_upload_bytes, result.depth_upload_bytes, result.stencil_upload_bytes,
        result.attachment_snapshot_fallbacks, result.depth.size() * sizeof(float), result.stencil.size());
}
}

void ConfigureNativeTextureTracking(bool enabled) {
    texture_tracking = enabled;
    std::printf("{\"event\":\"native_texture_validation_configuration\",\"mode\":\"%s\"}\n", enabled ? "tracked" : "full");
}
void ConfigureNativeAttachmentReuse(bool enabled) {
    attachment_reuse = enabled;
    std::printf("{\"event\":\"native_attachment_validation_configuration\",\"mode\":\"%s\"}\n", enabled ? "reuse" : "upload");
}
NativeGpuExecution ExecuteNativeGpuCommands(NativeVideoGpuContext& target,
    std::span<const std::vector<std::uint32_t>> streams, kt::gfx::pm4::Analysis& state, kt::gfx::ShaderMemoryReader reader) {
    using namespace kt::gfx::pm4;
    NativeGpuExecution result;
    for (const auto& words : streams) {
        for (unsigned offset = 0; offset < words.size();) {
            const auto decoded = Decode(words, offset);
            if (decoded.error != Error::None || Advance(decoded.packet, state) != Error::None) {
                throw std::runtime_error("PM4 execution frontier at word " + std::to_string(offset));
            }
            const auto& packet = decoded.packet;
            if (target.resident && !packet.padding) {
                if (packet.opcode == 0x69 || packet.opcode == 0x76 || packet.opcode == 0x79) {
                    const unsigned base = packet.opcode == 0x69 ? 0xa000u : packet.opcode == 0x76 ? 0x2c00u : 0xc000u;
                    target.resident->Registers(base + packet.body[0], packet.body.subspan(1));
                    if (packet.opcode == 0x69 && packet.body[0] >= 0x318 && packet.body[0] <= 0x381 && (packet.body[0] - 0x318) % 15 == 0) {
                        unsigned hint{};
                        const auto count = packet.body.size() - 1;
                        if ((count == 0xe || count == 0xd || count == 0xb) && decoded.next < words.size()) {
                            const auto next = Decode(words, decoded.next);
                            if (next.error == Error::None && next.packet.header == 0xc0001000 && next.packet.body.size() == 1) { hint = next.packet.body[0]; }
                        }
                        target.resident->Extent((packet.body[0] - 0x318) / 15, hint & 0xffffu, hint >> 16);
                    }
                }
                if (packet.opcode == 0x12) { target.resident->Clear(); }
            }
            if (!packet.padding && packet.opcode == 0x58) {
                // Strong synchronous profile: all earlier draws completed
                // their GPU fences and wrote guest memory back. Drain the
                // device explicitly before subsequent CPU snapshots/uploads.
                // Cached shaders/textures compare current owned contents;
                // this is not an address-only cache-invalidation shortcut.
                if (target.resident) { target.resident->Drain(); }
                else { target.device->WaitIdle(); }
                std::atomic_thread_fence(std::memory_order_seq_cst);
                const auto& acquire = *state.last_acquire;
                static std::atomic<unsigned> observed{};
                if (acquire.control != 0x2ec47fc0u && observed.fetch_add(1) < 16) {
                    std::printf("{\"event\":\"native_gpu_acquire_completed\",\"control\":\"0x%x\",\"base\":\"0x%llx\",\"bytes\":%llu,\"poll_interval\":%u,\"device_idle_completed\":true,\"profile\":\"synchronous_writeback\"}\n",
                        acquire.control, acquire.base, acquire.bytes, acquire.interval);
                }
            }
            if (!packet.padding && packet.opcode == 0x12) {
                // GCN CLEAR_STATE restores context defaults. Keep the journal's
                // unknown reset for unestablished registers; install only the
                // two hardware defaults consumed by this raster path.
                // AMD's Linux clearstate_ci.h (v6.12): CB_TARGET_MASK and
                // PA_SU_SC_MODE_CNTL, not fpPS4/shadPS4's 0xdeadbeef marker.
                state.context.values[0x8e] = 0xffffffffu; state.context.known.set(0x8e);
                state.context.values[0x205] = 4; state.context.known.set(0x205);
            }
            if (!packet.padding && packet.opcode == 0x3c) {
                const auto& wait = *state.last_wait;
                bool matched = false;
                for (auto& label : target.labels) {
                    if (reinterpret_cast<std::uint64_t>(&label) == wait.address) {
                        matched = true;
                        if ((static_cast<unsigned>(label.load()) & wait.mask) != wait.reference) { throw std::runtime_error("Live video buffer label is still busy"); }
                    }
                }
                if (!matched) { throw std::runtime_error("WAIT_REG_MEM is not a live video label"); }
            }
            if (!packet.padding && packet.opcode == 0x50) {
                const auto& dma = *state.last_dma;
                if (dma.control != 0x60000000 || dma.destination != 0x3022c || (dma.command & ~0x1fffffu) != 0x28200000 || !dma.bytes) {
                    throw std::runtime_error("Unimplemented non-prefetch DMA operation");
                }
                std::vector<std::uint8_t> source(dma.bytes);
                const bool readable = target.resident ? target.resident->Read(dma.source, source.data(), source.size())
                    : reader(nullptr, dma.source, source.data(), source.size());
                if (!readable) { throw std::runtime_error("Shader cache prefetch source is not readable"); }
            }
            if (!packet.padding && packet.opcode == 0x2d) {
                if (target.resident) {
                    if (!state.instances) { throw std::runtime_error("Missing resident draw instance count"); }
                    target.resident->Draw(packet.body[0], *state.instances);
                } else { ExecuteDraw(target, state, reader); }
                ++result.draws;
            }
            if (!packet.padding && packet.opcode == 0x10 && !packet.body.empty() && packet.body[0] == 0x68750777) {
                if (!PlainFlipReservation(packet, result.prepared_flip, &words == &streams.back(), decoded.next == words.size())) {
                    throw std::runtime_error("Invalid plain PrepareFlip reservation");
                }
                result.prepared_flip = true;
            }
            offset = decoded.next;
        }
    }
    return result;
}

bool GpuAdapterSelfTest(kt::gfx::VulkanDevice& device) {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value; std::printf("{\"event\":\"gpu_adapter_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    for (const unsigned bytes : {1u, 2u}) {
        constexpr unsigned pitch = 1280, height = 768;
        std::vector<bool> seen(std::size_t{pitch} * height);
        bool exact = true;
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < pitch; ++x) {
                const auto offset = DepthOffset(x, y, pitch, height, bytes);
                const auto pixel = offset / bytes;
                if (offset % bytes || pixel >= seen.size() || seen[pixel]) { exact = false; }
                else { seen[pixel] = true; }
            }
        }
        check(bytes == 1 ? "stencil_tile_mapping_is_bounded_bijective_over_entire_backing" : "z16_split_tile_mapping_is_bounded_bijective_over_entire_backing",
            exact && std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
    }
    check("depth_split_planes_and_bank_rotation_known_vectors", DepthOffset(0, 0, 1280, 768, 2) == 0 &&
        DepthOffset(1, 0, 1280, 768, 2) == 2 && DepthOffset(0, 4, 1280, 768, 2) == 1001472 &&
        DepthOffset(0, 4, 1280, 768, 1) == 32);
    const auto offsets = DepthOffsets(1280, 768, 16, 9, 1);
    bool lookup_exact = true;
    for (unsigned y = 0; y < 9; ++y) { for (unsigned x = 0; x < 16; ++x) { lookup_exact &= offsets[y * 16 + x] == DepthOffset(x, y, 1280, 768, 1); } }
    check("depth_lookup_preserves_formula_and_rebuilds_on_extent_change", lookup_exact && DepthOffsets(1280, 768, 8, 8, 1).size() == 64 && DepthOffsets(1280, 768, 16, 9, 2).size() == 144);
    std::vector<std::uint32_t> flip(64, 0xdeadbeef); flip[0] = 0xc03e1000; flip[1] = 0x68750777;
    flip[7] = 0xc0012d00; flip[8] = 4; flip[9] = 2; flip[25] = 0x68750777;
    const auto packet = kt::gfx::pm4::Decode(flip, 0);
    const auto journal = kt::gfx::pm4::Analyze(flip);
    check("plain_flip_reused_nop_payload_is_opaque_not_a_command_stream", journal.Complete() && journal.draws == 0 &&
        packet.next == 64 && PlainFlipReservation(packet.packet, false, true, true));
    check("plain_flip_still_requires_exact_extent_tail_and_single_reservation", !PlainFlipReservation(packet.packet, true, true, true) &&
        !PlainFlipReservation(packet.packet, false, false, true) && !PlainFlipReservation(packet.packet, false, true, false));
    const kt::gfx::DisplayLayout layout{};
    std::array<std::uint8_t, 16> backing{}; backing.fill(73);
    std::array<std::atomic<std::uint64_t>, 1> labels{}; labels[0].store(91);
    NativeVideoGpuContext target{device, layout, backing, 0, labels};
    const std::array<std::vector<std::uint32_t>, 1> acquires{std::vector<std::uint32_t>{
        0xc0055800, 0x82c40040, 0x200, 0, 0x12345678, 0, 10,
        0xc0055800, 0x88000000, 1, 0, 0, 0, 10}};
    kt::gfx::pm4::Analysis acquire_state;
    const auto completed = ExecuteNativeGpuCommands(target, acquires, acquire_state, nullptr);
    check("regional_acquire_drains_real_device_without_dereferencing_cache_range_or_completing_a_flip",
        completed.draws == 0 && !completed.prepared_flip && acquire_state.acquires == 2 &&
        acquire_state.last_acquire->bytes == 256 && labels[0].load() == 91 &&
        std::all_of(backing.begin(), backing.end(), [](auto byte) { return byte == 73; }));
    const auto display_layout = kt::gfx::DescribeDisplay({0x80000000u, 0, 0, 1280, 720, 1280, 0, 0, 0}, false).layout;
    NativeVideoGpuContext display{device, display_layout, {}, 0x100000, labels};
    kt::gfx::pm4::RegisterBank color;
    const auto set = [&](unsigned index, unsigned value) { color.values[index] = value; color.known.set(index); };
    set(0x318, 0x1000); set(0x319, 159); set(0x31a, 15359); set(0x31b, 0); set(0x31c, 0x8828); set(0x31d, 0x14a); set(0x202, 0xcc0010);
    const auto onscreen = ColorAttachment(color, display);
    set(0x318, 0x2000); set(0x31d, 0x1ce); const auto offscreen = ColorAttachment(color, display);
    check("color_attachment_uses_own_backing_and_padded_extent_not_display_crop", onscreen.address == display.address && onscreen.height == 720 &&
        offscreen.address == 0x200000 && offscreen.height == 768 && offscreen.width == 1280 && offscreen.tiling == 14 && offscreen.bytes == 3932160);
    const auto rejects = [&] { try { (void)ColorAttachment(color, display); return false; } catch (const std::exception&) { return true; } };
    set(0x31d, 0x11ce); const bool msaa_rejected = rejects(); set(0x31d, 0x1ce);
    set(0x31b, 1); const bool array_rejected = rejects(); set(0x31b, 0);
    set(0x31c, 0xc828); const bool compression_rejected = rejects(); set(0x31c, 0x8828);
    set(0x31a, 15358); const bool extent_rejected = rejects(); set(0x31a, 15359);
    set(0x318, 0x2001);
    check("color_attachment_rejects_msaa_array_compression_extent_and_bank_swizzle", msaa_rejected && array_rejected && compression_rejected && extent_rejected && rejects());
    return kt::memory::TrackedTextureGpuSelfTest(device) && passed;
}
