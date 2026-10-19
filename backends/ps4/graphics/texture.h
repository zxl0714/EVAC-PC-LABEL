#pragma once
#include "raster.h"
#include <cstdint>
#include <span>
#include <vector>
#include <memory>
#include <list>

namespace kt::gfx {
struct TextureLayout {
    std::uint64_t address{}, bytes{};
    std::uint32_t width{}, height{}, pitch{}, padded_height{}, tiling{};
    std::uint32_t channels = 4;
    bool srgb{};
    std::array<std::uint32_t, 4> components{};
    bool operator==(const TextureLayout&) const = default;
};
class TextureDecodeCache {
public:
    explicit TextureDecodeCache(std::uint64_t maximum_bytes = 512 * 1024 * 1024, std::size_t maximum_entries = 16) : maximum_bytes_(maximum_bytes), maximum_entries_(maximum_entries) {}
    // The complete current backing is compared on every call. A hit borrows
    // it only during comparison; a miss takes an owned snapshot before decode.
    // Caller keeps the view valid/readable for this call; no guest view escapes.
    std::shared_ptr<const std::vector<std::uint8_t>> Decode(const TextureLayout& layout, std::span<const std::uint8_t> snapshot);
    std::uint64_t hits{}, misses{}, evictions{};
    std::uint64_t Bytes() const { return bytes_; }
private:
    struct Entry { TextureLayout layout; std::vector<std::uint8_t> snapshot; std::shared_ptr<const std::vector<std::uint8_t>> pixels; std::uint64_t bytes; };
    std::list<Entry> entries_;
    std::uint64_t maximum_bytes_, bytes_{};
    std::size_t maximum_entries_;
};
TextureLayout DescribeTexture(std::span<const std::uint32_t, 8> descriptor);
std::uint64_t TextureByteOffset(const TextureLayout& layout, unsigned x, unsigned y);
std::vector<std::uint8_t> ReadTexturePixels(const TextureLayout& layout, std::span<const std::uint8_t> backing);
// Exact raw RGBA storage writeback for supported 32bpp layouts; padding is kept.
void WriteTexturePixels(const TextureLayout& layout, std::span<const std::uint8_t> pixels, std::span<std::uint8_t> backing);
RasterSampler DescribeSampler(std::span<const std::uint32_t, 4> descriptor);
}
