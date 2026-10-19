#include "gpu_program.h"
#include "cache_memory.h"
#include "common/hash.h"
#include "video_core/amdgpu/regs_shader.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <xxhash.h>
namespace Ketsui::GpuCore {
namespace { thread_local ProgramStore* current; }
ProgramScope::ProgramScope(ProgramStore& store) : previous{current} { current = &store; }
ProgramScope::~ProgramScope() { current = previous; }
Shader::ShaderParams ReadProgram(VAddr address, std::span<const u32, 16> user_data) {
    constexpr u64 limit = 1024 * 1024;
    if (!current || !address || address % 256 || address > UINT64_MAX - limit - 32) {
        throw std::runtime_error("Shader program address/store is invalid");
    }
    std::array<u32, 2> header;
    CacheMemory().ReadProgram(address, header.data(), sizeof(header));
    if (header[0] != 0xbeeb03ffu) { throw std::runtime_error("Shader program has no bounded entry declaration"); }
    const u64 offset = (u64{header[1]} + 1) * 8;
    if (offset < 8 || offset > limit) { throw std::runtime_error("Shader metadata exceeds the program window"); }
    AmdGpu::BinaryInfo info{};
    constexpr auto info_bytes = offsetof(AmdGpu::BinaryInfo, crc32) + sizeof(info.crc32);
    CacheMemory().ReadProgram(address + offset, &info, info_bytes);
    if (!info.Valid() || info.version != 7 || info.length < sizeof(header) || info.length % 4 || info.length > offset) {
        throw std::runtime_error("Shader signature/version/code extent is invalid");
    }
    std::vector<u32> code(info.length / 4);
    CacheMemory().ReadProgram(address, code.data(), info.length);
    const auto hash = HashCombine(u64{XXH3_64bits(code.data(), info.length)}, HashCombine(info.shader_hash, u64{info.type}));
    auto [it, inserted] = current->programs.try_emplace(hash);
    if (inserted) { it->second = std::move(code); }
    else if (it->second != code) { throw std::runtime_error("Shader program content hash collision"); }
    return {.user_data=user_data, .code=it->second, .hash=hash, .guest_base=address};
}
}
