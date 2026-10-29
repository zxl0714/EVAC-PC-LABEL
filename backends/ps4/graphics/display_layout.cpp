#include "display_layout.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <mutex>
#include <list>

namespace kt::gfx {
namespace {
std::uint32_t Align(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
}

LayoutResult DescribeDisplay(const DisplayAttribute& attribute, bool neo) {
    if (attribute.reserved0 || attribute.reserved1) { return {LayoutError::Value, {}}; }
    // SDR video-out uses Display2DThin on both machines; NEO's alternate
    // texture tiling is handled by each GPU resource descriptor, not this API.
    (void)neo;
    if (attribute.option) { return {LayoutError::Unsupported, {}}; }
    if (attribute.aspect) { return {LayoutError::Aspect, {}}; }
    if (attribute.tiling > 1) { return {LayoutError::Tiling, {}}; }
    ColorFormat format{};
    switch (attribute.format) {
    case 0x80000000u: format = ColorFormat::Bgra8Srgb; break;
    case 0x80002200u: format = ColorFormat::Rgba8Srgb; break;
    default: return {LayoutError::Format, {}};
    }
    if (!attribute.width || !attribute.height || attribute.width > 16384 || attribute.height > 16384) { return {LayoutError::Resolution, {}}; }
    if (attribute.pitch < attribute.width || attribute.pitch > 16384) { return {LayoutError::Pitch, {}}; }
    DisplayLayout layout{};
    layout.format = format;
    layout.width = attribute.width;
    layout.height = attribute.height;
    layout.tiled = attribute.tiling == 0;
    if (layout.tiled) {
        // Base Display2DThin, 32 bpp, one sample: a 128x64 macro tile.
        // This is a layout parameter, not a generic width*height estimate.
        layout.pitch = Align(attribute.pitch, 128);
        layout.padded_height = Align(attribute.height, 64);
        layout.alignment = 32768;
    } else {
        layout.pitch = Align(attribute.pitch, 16);
        layout.padded_height = attribute.height;
        while ((static_cast<std::uint64_t>(layout.pitch) * layout.padded_height) % 64) { layout.pitch += 16; }
        layout.alignment = 256;
    }
    layout.bytes = static_cast<std::uint64_t>(layout.pitch) * layout.padded_height * 4;
    if (layout.bytes > 512ull * 1024 * 1024) { return {LayoutError::Resolution, {}}; }
    return {LayoutError::None, layout};
}

std::uint64_t DisplayByteOffset(const DisplayLayout& layout, std::uint32_t x, std::uint32_t y) {
    if (x >= layout.pitch || y >= layout.padded_height) { throw std::out_of_range("Display pixel outside backing"); }
    if (!layout.tiled) { return (std::uint64_t{y} * layout.pitch + x) * 4; }
    return MacroTile32ByteOffset(layout.pitch, x, y, true);
}
std::uint64_t MacroTile32ByteOffset(std::uint32_t pitch, std::uint32_t x, std::uint32_t y, bool display_microtile) {
    if (!pitch || pitch % 128 || x >= pitch) { throw std::out_of_range("Invalid 32bpp macrotiling pitch"); }
    // Base Display2DThin, 8 pipes, 16 banks, 32bpp, one sample, no slice XOR.
    const auto micro = display_microtile ?
        (x & 3u) | ((y & 1u) << 2) | ((x & 4u) << 1) | ((y & 6u) << 3) :
        (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) | ((y & 2u) << 2) | ((x & 4u) << 2) | ((y & 4u) << 3);
    const auto bit = [](unsigned value, unsigned index) { return (value >> index) & 1u; };
    const auto pipe = (bit(x, 3) ^ bit(y, 3) ^ bit(x, 4)) |
        ((bit(x, 4) ^ bit(y, 4)) << 1) | ((bit(x, 5) ^ bit(y, 5)) << 2);
    const auto tx = x >> 6, ty = y >> 3;
    const auto bank = (bit(tx, 0) ^ bit(ty, 3)) | ((bit(tx, 1) ^ bit(ty, 2) ^ bit(ty, 3)) << 1) |
        ((bit(tx, 2) ^ bit(ty, 1)) << 2) | ((bit(tx, 3) ^ bit(ty, 0)) << 3);
    const auto macro = std::uint64_t{y / 64} * (pitch / 128) + x / 128;
    return std::uint64_t{micro * 4} | (std::uint64_t{pipe} << 8) | (std::uint64_t{bank} << 11) | (macro << 15);
}
namespace {
std::shared_ptr<const std::vector<std::uint32_t>> Offsets(const DisplayLayout& layout) {
    struct Entry { DisplayLayout layout; std::shared_ptr<const std::vector<std::uint32_t>> offsets; };
    static std::mutex mutex;
    static std::list<Entry> entries;
    static std::uint64_t bytes{};
    constexpr std::uint64_t limit = 64 * 1024 * 1024;
    const auto count = std::uint64_t{layout.width} * layout.height;
    if (count > limit / 4 || layout.bytes > UINT32_MAX) { return {}; }
    std::lock_guard lock(mutex);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->layout == layout) { auto result = it->offsets; entries.splice(entries.begin(), entries, it); return result; }
    }
    auto result = std::make_shared<std::vector<std::uint32_t>>(static_cast<std::size_t>(count));
    for (unsigned y = 0; y < layout.height; ++y) { for (unsigned x = 0; x < layout.width; ++x) {
        const auto offset = DisplayByteOffset(layout, x, y);
        if (offset > layout.bytes || layout.bytes - offset < 4) { throw std::runtime_error("Invalid display lookup offset"); }
        (*result)[std::size_t{y} * layout.width + x] = static_cast<std::uint32_t>(offset);
    } }
    while (!entries.empty() && (entries.size() >= 8 || bytes > limit - count * 4)) {
        bytes -= entries.back().offsets->size() * 4; entries.pop_back();
    }
    entries.push_front({layout, result}); bytes += count * 4; return result;
}
}
std::vector<std::uint8_t> ReadDisplayPixels(const DisplayLayout& layout, std::span<const std::uint8_t> tiled) {
    if (tiled.size() != layout.bytes) { throw std::runtime_error("Display backing extent mismatch"); }
    std::vector<std::uint8_t> linear(std::size_t{layout.width} * layout.height * 4);
    if (const auto offsets = Offsets(layout)) {
        for (std::size_t index = 0; index < offsets->size(); ++index) { std::memcpy(linear.data() + index * 4, tiled.data() + (*offsets)[index], 4); }
        return linear;
    }
    for (unsigned y = 0; y < layout.height; ++y) {
        for (unsigned x = 0; x < layout.width; ++x) {
            const auto offset = DisplayByteOffset(layout, x, y);
            if (offset > tiled.size() || tiled.size() - offset < 4) { throw std::runtime_error("Invalid tiled display offset"); }
            std::memcpy(linear.data() + (std::size_t{y} * layout.width + x) * 4, tiled.data() + offset, 4);
        }
    }
    return linear;
}
void WriteDisplayPixels(const DisplayLayout& layout, std::span<const std::uint8_t> linear, std::span<std::uint8_t> tiled) {
    if (tiled.size() != layout.bytes || linear.size() != std::size_t{layout.width} * layout.height * 4) { throw std::runtime_error("Display writeback extent mismatch"); }
    if (const auto offsets = Offsets(layout)) {
        for (std::size_t index = 0; index < offsets->size(); ++index) { std::memcpy(tiled.data() + (*offsets)[index], linear.data() + index * 4, 4); }
        return;
    }
    for (unsigned y = 0; y < layout.height; ++y) {
        for (unsigned x = 0; x < layout.width; ++x) {
            const auto offset = DisplayByteOffset(layout, x, y);
            if (offset > tiled.size() || tiled.size() - offset < 4) { throw std::runtime_error("Invalid tiled display offset"); }
            std::memcpy(tiled.data() + offset, linear.data() + (std::size_t{y} * layout.width + x) * 4, 4);
        }
    }
}

bool DisplayLayoutSelfTest() {
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"display_layout_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    DisplayAttribute attribute{0x80000000u, 0, 0, 1280, 720, 1280, 0, 0, 0};
    auto result = DescribeDisplay(attribute, false);
    check("game_720p_tiled_footprint", result.error == LayoutError::None && result.layout.pitch == 1280 && result.layout.padded_height == 768 &&
          result.layout.bytes == 3932160 && result.layout.alignment == 32768 && result.layout.format == ColorFormat::Bgra8Srgb);
    attribute.format = 0x80002200u;
    result = DescribeDisplay(attribute, false);
    check("rgba_preserves_channel_format", result.error == LayoutError::None && result.layout.format == ColorFormat::Rgba8Srgb);
    attribute = {0x80000000u, 1, 0, 1920, 1080, 1920, 0, 0, 0};
    result = DescribeDisplay(attribute, false);
    check("linear_1080p", result.error == LayoutError::None && result.layout.bytes == 8294400 && result.layout.alignment == 256 && !result.layout.tiled);
    attribute.width = 17; attribute.pitch = 17; attribute.height = 3;
    result = DescribeDisplay(attribute, false);
    check("linear_row_and_slice_alignment", result.error == LayoutError::None && result.layout.pitch == 64 && result.layout.bytes == 768);
    attribute.width = 0;
    check("invalid_dimensions_clear_layout", DescribeDisplay(attribute, false).error == LayoutError::Resolution && DescribeDisplay(attribute, false).layout.bytes == 0);
    attribute = {0x80000000u, 0, 0, 1280, 720, 1, 0, 0, 0};
    const bool invalid_pitch = DescribeDisplay(attribute, false).error == LayoutError::Pitch;
    attribute.pitch = 1280; attribute.format = 0;
    const bool invalid_format = DescribeDisplay(attribute, false).error == LayoutError::Format;
    attribute.format = 0x80000000u; attribute.reserved1 = 1;
    check("invalid_pitch_format_and_reserved", invalid_pitch && invalid_format && DescribeDisplay(attribute, false).error == LayoutError::Value);
    attribute.reserved1 = 0;
    check("neo_sdr_preserves_display_layout", DescribeDisplay(attribute, true).error == LayoutError::None &&
          DescribeDisplay(attribute, true).layout==DescribeDisplay(attribute, false).layout);
    const auto tiled_layout = DescribeDisplay({0x80000000u, 0, 0, 120, 60, 128, 0, 0, 0}, false).layout;
    check("display_tiling_known_coordinate_vectors", DisplayByteOffset(tiled_layout, 0, 0) == 0 && DisplayByteOffset(tiled_layout, 4, 0) == 32 &&
        DisplayByteOffset(tiled_layout, 0, 2) == 64 && DisplayByteOffset(tiled_layout, 8, 0) == 256 &&
        DisplayByteOffset(tiled_layout, 0, 8) == 16640 && DisplayByteOffset(tiled_layout, 64, 0) == 2048);
    std::vector<bool> coverage(128 * 64);
    bool bijective = true;
    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 128; ++x) {
            const auto offset = DisplayByteOffset(tiled_layout, x, y) / 4;
            if (offset >= coverage.size() || coverage[offset]) { bijective = false; }
            else { coverage[offset] = true; }
        }
    }
    check("display_macro_tile_has_no_aliases_or_holes", bijective && std::all_of(coverage.begin(), coverage.end(), [](bool value) { return value; }));
    std::vector<std::uint8_t> raw(32768, 73), linear(120 * 60 * 4);
    for (std::size_t index = 0; index < linear.size(); ++index) { linear[index] = static_cast<std::uint8_t>(index * 37); }
    WriteDisplayPixels(tiled_layout, linear, raw);
    check("display_roundtrip_preserves_nonvisible_padding", ReadDisplayPixels(tiled_layout, raw) == linear &&
        raw[DisplayByteOffset(tiled_layout, 120, 1)] == 73 && raw[DisplayByteOffset(tiled_layout, 0, 63)] == 73);
    std::printf("{\"event\":\"display_layout_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
} // namespace kt::gfx
