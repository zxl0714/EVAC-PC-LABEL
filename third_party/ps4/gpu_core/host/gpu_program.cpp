#include "gpu_program.h"
#include "draw_timing.h"
#include "cache_memory.h"
#include "common/hash.h"
#include "video_core/amdgpu/regs_shader.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <xxhash.h>
namespace Ketsui::GpuCore {
namespace {
thread_local ProgramStore* current;
struct ReadScratch {
    ProgramStore& store;
    std::vector<u32> nested;
    const bool leased;
    explicit ReadScratch(ProgramStore& value) : store(value), leased(!value.scratch_leased) {
        if (leased) { store.scratch_leased = true; }
    }
    ~ReadScratch() { if (leased) { store.scratch_leased = false; } }
    ReadScratch(const ReadScratch&) = delete;
    std::vector<u32>& Words() { return leased ? store.read_scratch : nested; }
};
}
ProgramScope::ProgramScope(ProgramStore& store) : previous{current} { current = &store; }
ProgramScope::~ProgramScope() { current = previous; }
Shader::ShaderParams ReadProgram(VAddr address, std::span<const u32, 16> user_data) {
    ProgramReadTimingScope timing;
    constexpr u64 limit = 1024 * 1024;
    if (!current || !address || address % 256 || address > UINT64_MAX - limit - 32) {
        throw std::runtime_error("Shader program address/store is invalid");
    }
    ReadScratch lease(*current);
    auto& code = lease.Words();
    std::array<u32, 2> header;
    u64 prefetched{};
    if (const auto extent = current->read_extents.find(address); extent != current->read_extents.end()) {
        code.resize(extent->second / 4);
        try {
            // Read the current header AND full previous code extent together.
            // A shortened/remapped allocation may no longer cover that guess;
            // fall back to the original bounded header/metadata/code reads.
            CacheMemory().ReadProgram(address, code.data(), extent->second);
            prefetched = extent->second;
            std::memcpy(header.data(), code.data(), sizeof(header));
        } catch (const std::runtime_error&) { prefetched = 0; }
    }
    if (!prefetched) { CacheMemory().ReadProgram(address, header.data(), sizeof(header)); }
    if (header[0] != 0xbeeb03ffu) { throw std::runtime_error("Shader program has no bounded entry declaration"); }
    const u64 offset = (u64{header[1]} + 1) * 8;
    if (offset < 8 || offset > limit) { throw std::runtime_error("Shader metadata exceeds the program window"); }
    AmdGpu::BinaryInfo info{};
    constexpr auto info_bytes = offsetof(AmdGpu::BinaryInfo, crc32) + sizeof(info.crc32);
    CacheMemory().ReadProgram(address + offset, &info, info_bytes);
    if (!info.Valid() || info.version != 7 || info.length < sizeof(header) || info.length % 4 || info.length > offset) {
        throw std::runtime_error("Shader signature/version/code extent is invalid");
    }
    if (prefetched < info.length) {
        code.resize(info.length / 4);
        CacheMemory().ReadProgram(address, code.data(), info.length);
    }
    const auto hash = HashCombine(u64{XXH3_64bits(code.data(), info.length)}, HashCombine(info.shader_hash, u64{info.type}));
    auto it = current->programs.find(hash);
    const std::size_t words = info.length / 4;
    if (it == current->programs.end()) {
        // Allocate before publishing the key: allocation failure must not leave
        // an empty, permanently poisoned content-cache entry.
        std::vector<u32> owned(code.begin(), code.begin() + words);
        it = current->programs.emplace(hash, std::move(owned)).first;
    } else if (it->second.size() != words || !std::equal(it->second.begin(), it->second.end(), code.begin())) {
        throw std::runtime_error("Shader program content hash collision");
    }
    current->read_extents.insert_or_assign(address, static_cast<u32>(info.length));
    return {.user_data=user_data, .code=it->second, .hash=hash, .guest_base=address};
}
}
