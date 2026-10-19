#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kt::gfx {
struct DisplayAttribute {
    std::uint32_t format, tiling, aspect, width, height, pitch, option, reserved0;
    std::uint64_t reserved1;
};
static_assert(sizeof(DisplayAttribute) == 40 && offsetof(DisplayAttribute, option) == 24);

enum class ColorFormat { Bgra8Srgb, Rgba8Srgb };
enum class LayoutError { None, Value, Format, Tiling, Aspect, Pitch, Resolution, Unsupported };
struct DisplayLayout {
    ColorFormat format{};
    std::uint32_t width{}, height{}, pitch{}, padded_height{};
    std::uint64_t bytes{}, alignment{};
    bool tiled{};
    bool operator==(const DisplayLayout&) const = default;
};
struct LayoutResult { LayoutError error{}; DisplayLayout layout{}; };

// Single-sample SDR display surfaces. GPU image allocation is deliberately
// distinct from the padded guest layout. This does not detile pixels.
LayoutResult DescribeDisplay(const DisplayAttribute& attribute, bool neo);
bool DisplayLayoutSelfTest();
std::uint64_t DisplayByteOffset(const DisplayLayout& layout, std::uint32_t x, std::uint32_t y);
// Base-machine, single-sample 32bpp 2D macrotiling, no bank/slice XOR.
// Display2DThin and Thin2DThin differ only in their 8x8 microtile order.
std::uint64_t MacroTile32ByteOffset(std::uint32_t pitch, std::uint32_t x, std::uint32_t y, bool display_microtile);
std::vector<std::uint8_t> ReadDisplayPixels(const DisplayLayout& layout, std::span<const std::uint8_t> tiled);
void WriteDisplayPixels(const DisplayLayout& layout, std::span<const std::uint8_t> linear, std::span<std::uint8_t> tiled);
} // namespace kt::gfx
