#pragma once
#include "shader_compiler.h"

namespace kt::gfx {
struct ShaderBinary {
    ShaderStage stage;
    std::uint32_t metadata_offset{}, metadata_version{};
    std::uint64_t declared_hash{};
    std::vector<std::uint32_t> instructions;
};
// Read the bounded OrbShdr declaration; do not search arbitrary memory for an
// instruction that happens to have the END encoding. All reads use the host.
ShaderBinary ReadShaderBinary(ShaderMemoryReader reader, void* context, std::uint64_t address);
}
