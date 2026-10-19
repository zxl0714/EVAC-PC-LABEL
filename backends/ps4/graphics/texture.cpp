#include "texture.h"
#include "display_layout.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kt::gfx {
namespace { void ValidatePixels(const TextureLayout& layout, std::size_t bytes); }
std::shared_ptr<const std::vector<std::uint8_t>> TextureDecodeCache::Decode(const TextureLayout& layout, std::span<const std::uint8_t> snapshot) {
    ValidatePixels(layout, snapshot.size());
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->layout == layout && std::equal(snapshot.begin(), snapshot.end(), it->snapshot.begin(), it->snapshot.end())) {
            const auto pixels = it->pixels; entries_.splice(entries_.begin(), entries_, it); ++hits; return pixels;
        }
    }
    ++misses;
    // The caller may supply a pinned, borrowed current-memory view. Snapshot
    // once on a miss so cached bytes and decoded pixels describe the same
    // owned input, even after that view is released or later changes.
    std::vector<std::uint8_t> owned(snapshot.begin(), snapshot.end());
    auto pixels = std::make_shared<const std::vector<std::uint8_t>>(ReadTexturePixels(layout, owned));
    const auto bytes = std::uint64_t{owned.size()} + pixels->size();
    if (maximum_entries_ && bytes <= maximum_bytes_) {
        while (!entries_.empty() && (entries_.size() >= maximum_entries_ || bytes_ > maximum_bytes_ - bytes)) {
            bytes_ -= entries_.back().bytes; entries_.pop_back(); ++evictions;
        }
        entries_.push_front({layout, std::move(owned), pixels, bytes}); bytes_ += bytes;
    }
    return pixels;
}
TextureLayout DescribeTexture(std::span<const std::uint32_t, 8> words) {
    const auto a = std::uint64_t{words[0]} | (std::uint64_t{words[1]} << 32);
    const auto b = std::uint64_t{words[2]} | (std::uint64_t{words[3]} << 32);
    const auto c = std::uint64_t{words[4]} | (std::uint64_t{words[5]} << 32);
    TextureLayout result;
    result.address = (a & 0x3fffffffffull) << 8;
    result.width = (words[2] & 16383) + 1; result.height = ((words[2] >> 14) & 16383) + 1;
    result.pitch = ((words[4] >> 13) & 16383) + 1; result.tiling = (words[3] >> 20) & 31;
    const auto format = (a >> 52) & 63, number = (a >> 58) & 15;
    result.channels = format == 1 ? 1u : format == 3 ? 2u : format == 10 ? 4u : 0u;
    // Finite contract: 2D, single sample/layer/mip; R8/RG8/RGBA8 linear or
    // Thin1DThin; 32bpp also supports Display2DThin / Thin2DThin. No
    // compression, bank XOR, arrays, depth or stencil.
    if (!result.address || (b >> 60) != 9 || !result.channels || (number != 0 && number != 9) ||
        ((b >> 44) & 255) || (c & 8191) || (c >> 32) || words[6] || words[7] ||
        (result.tiling != 8 && result.tiling != 13 && result.tiling != 10 && result.tiling != 14) || result.pitch < result.width ||
        result.width > 8192 || result.height > 8192 || (result.tiling == 13 && result.pitch % 8) ||
        ((result.tiling == 10 || result.tiling == 14) && (result.channels != 4 || result.pitch % 128))) {
        throw std::runtime_error("Texture needs an unsupported format/mip/array/tile contract");
    }
    result.padded_height = (result.tiling == 10 || result.tiling == 14) ? (result.height + 63) & ~63u :
        result.tiling == 13 ? (result.height + 7) & ~7u : result.height;
    result.bytes = std::uint64_t{result.pitch} * result.padded_height * result.channels;
    if (result.bytes > 64 * 1024 * 1024 || result.bytes > UINT64_MAX - result.address) { throw std::runtime_error("Texture backing exceeds snapshot bound"); }
    result.srgb = number == 9;
    for (unsigned index = 0; index < 4; ++index) {
        const auto component = (words[3] >> (index * 3)) & 7;
        if (component == 2 || component == 3) { throw std::runtime_error("Reserved texture component selector"); }
        result.components[index] = component;
    }
    return result;
}
std::uint64_t TextureByteOffset(const TextureLayout& layout, unsigned x, unsigned y) {
    if (x >= layout.pitch || y >= layout.padded_height) { throw std::out_of_range("Texture coordinate exceeds backing"); }
    if (layout.channels != 1 && layout.channels != 2 && layout.channels != 4) { throw std::runtime_error("Texture channel extent was not described"); }
    if (layout.tiling == 8) { return (std::uint64_t{y} * layout.pitch + x) * layout.channels; }
    if (layout.tiling == 10 || layout.tiling == 14) {
        if (layout.channels != 4 || layout.pitch % 128 || layout.padded_height % 64) { throw std::runtime_error("Invalid macrotiling layout"); }
        return MacroTile32ByteOffset(layout.pitch, x, y, layout.tiling == 10);
    }
    if (layout.tiling != 13 || layout.pitch % 8 || layout.padded_height % 8) { throw std::runtime_error("Texture layout was not described"); }
    // Thin microtiling uses Morton XY for 8/16/32bpp (unlike display-mode
    // microtiling). Row-major 8x8 tiles have no macro pipe/bank XOR bits.
    const auto micro = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return ((std::uint64_t{y / 8} * (layout.pitch / 8) + x / 8) * 64 + micro) * layout.channels;
}
namespace {
void ValidatePixels(const TextureLayout& layout, std::size_t bytes) {
    if (bytes != layout.bytes) { throw std::runtime_error("Texture backing size mismatch"); }
    if (!layout.width || !layout.height || layout.width > 8192 || layout.height > 8192 || layout.pitch < layout.width || layout.pitch > 16384 ||
        layout.padded_height < layout.height || layout.padded_height > 8192 ||
        (layout.tiling != 8 && layout.tiling != 13 && layout.tiling != 10 && layout.tiling != 14) ||
        (layout.tiling == 13 && (layout.pitch % 8 || layout.padded_height % 8)) ||
        ((layout.tiling == 10 || layout.tiling == 14) && (layout.channels != 4 || layout.pitch % 128 || layout.padded_height % 64)) ||
        (layout.channels != 1 && layout.channels != 2 && layout.channels != 4) ||
        std::uint64_t{layout.pitch} * layout.padded_height * layout.channels != layout.bytes || layout.bytes > 64 * 1024 * 1024 ||
        std::uint64_t{layout.width} * layout.height * 4 > 64 * 1024 * 1024) { throw std::runtime_error("Texture layout exceeds decoded pixel bounds"); }
}
}
std::vector<std::uint8_t> ReadTexturePixels(const TextureLayout& layout, std::span<const std::uint8_t> backing) {
    ValidatePixels(layout, backing.size());
    std::vector<std::uint8_t> pixels(std::size_t{layout.width} * layout.height * 4);
    if (layout.tiling == 8 && layout.channels == 4) {
        for (unsigned y = 0; y < layout.height; ++y) {
            std::memcpy(pixels.data() + std::size_t{y} * layout.width * 4, backing.data() + std::size_t{y} * layout.pitch * 4, std::size_t{layout.width} * 4);
        }
        return pixels;
    }
    for (unsigned y = 0; y < layout.height; ++y) {
        for (unsigned x = 0; x < layout.width; ++x) {
            const auto offset = TextureByteOffset(layout, x, y);
            if (offset > backing.size() || backing.size() - offset < layout.channels) { throw std::runtime_error("Texture tile offset exceeds backing"); }
            auto* output = pixels.data() + (std::size_t{y} * layout.width + x) * 4;
            if (layout.channels == 4) { std::memcpy(output, backing.data() + offset, 4); }
            else { output[0] = backing[offset]; output[1] = layout.channels == 2 ? backing[offset + 1] : 0; output[2] = 0; output[3] = 255; }
        }
    }
    return pixels;
}
void WriteTexturePixels(const TextureLayout& layout, std::span<const std::uint8_t> pixels, std::span<std::uint8_t> backing) {
    ValidatePixels(layout, backing.size());
    if (layout.channels != 4 || pixels.size() != std::uint64_t{layout.width} * layout.height * 4) {
        throw std::runtime_error("Color surface writeback extent/format mismatch");
    }
    for (unsigned y = 0; y < layout.height; ++y) { for (unsigned x = 0; x < layout.width; ++x) {
        const auto offset = TextureByteOffset(layout, x, y);
        if (offset > backing.size() || backing.size() - offset < 4) { throw std::runtime_error("Color surface offset exceeds backing"); }
        std::memcpy(backing.data() + offset, pixels.data() + (std::size_t{y} * layout.width + x) * 4, 4);
    } }
}
RasterSampler DescribeSampler(std::span<const std::uint32_t, 4> words) {
    const auto a = std::uint64_t{words[0]} | (std::uint64_t{words[1]} << 32);
    const auto b = std::uint64_t{words[2]} | (std::uint64_t{words[3]} << 32);
    RasterSampler result;
    const auto mag = (b >> 20) & 3, min = (b >> 22) & 3, mip = (b >> 26) & 3;
    if (((a >> 9) & 7) || (a & ((1ull << 15) | (1ull << 20) | (3ull << 29))) || mag > 1 || min > 1 || mip > 2 ||
        ((b >> 32) & 4095) || (b >> 62) == 3) { throw std::runtime_error("Sampler anisotropy/unnormalized/reduction/custom border contract pending"); }
    for (unsigned index = 0; index < 3; ++index) {
        result.clamp[index] = static_cast<unsigned>((a >> (index * 3)) & 7);
        if (result.clamp[index] != 0 && result.clamp[index] != 1 && result.clamp[index] != 2 && result.clamp[index] != 6) {
            throw std::runtime_error("Sampler mirror-once or half-border contract pending");
        }
    }
    result.linear_mag = mag == 1; result.linear_min = min == 1; result.linear_mip = mip == 2;
    const auto signed_bias = static_cast<std::int32_t>((b & 0x3fff) ^ 0x2000) - 0x2000;
    result.bias = static_cast<float>(signed_bias) / 256;
    result.minimum_lod = static_cast<float>((a >> 32) & 4095) / 256;
    result.maximum_lod = mip ? static_cast<float>((a >> 44) & 4095) / 256 : 0;
    if (result.minimum_lod > result.maximum_lod) { throw std::runtime_error("Invalid sampler LOD interval"); }
    result.border = static_cast<unsigned>(b >> 62); return result;
}
}
