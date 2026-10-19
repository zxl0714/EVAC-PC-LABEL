#include "shader_binary.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace kt::gfx {
ShaderBinary ReadShaderBinary(ShaderMemoryReader reader, void* context, std::uint64_t address) {
    constexpr std::uint64_t limit = 1024 * 1024;
    if (!reader || !address || address % 256 || address > UINT64_MAX - limit - 28) { throw std::runtime_error("Shader binary address is invalid"); }
    std::array<std::uint32_t, 2> header{};
    if (!reader(context, address, header.data(), sizeof(header)) || header[0] != 0xbeeb03ffu) { throw std::runtime_error("Shader binary entry declaration is missing"); }
    const std::uint64_t metadata_offset = (std::uint64_t{header[1]} + 1) * 8;
    if (metadata_offset < 8 || metadata_offset > limit) { throw std::runtime_error("Shader metadata offset exceeds the bounded binary window"); }
    std::array<std::uint32_t, 7> metadata{};
    if (!reader(context, address + metadata_offset, metadata.data(), sizeof(metadata))) { throw std::runtime_error("Shader metadata is not readable"); }
    const auto* bytes = reinterpret_cast<const unsigned char*>(metadata.data());
    if (std::memcmp(bytes, "OrbShdr", 7) || bytes[7] != 7) { throw std::runtime_error("Shader metadata signature or version is unsupported"); }
    const auto length = metadata[2] >> 8;
    const auto kind = (metadata[2] >> 2) & 15;
    if (!length || length % 4 || length > metadata_offset || kind > 1) { throw std::runtime_error("Shader code length or stage declaration is invalid"); }
    ShaderBinary result{}; result.stage = kind == 1 ? ShaderStage::Vertex : ShaderStage::Pixel;
    result.metadata_offset = static_cast<std::uint32_t>(metadata_offset); result.metadata_version = bytes[7];
    result.declared_hash = metadata[4] | (std::uint64_t{metadata[5]} << 32);
    result.instructions.resize(length / 4);
    if (!reader(context, address, result.instructions.data(), length)) { throw std::runtime_error("Shader instruction range is not readable"); }
    return result;
}
}
