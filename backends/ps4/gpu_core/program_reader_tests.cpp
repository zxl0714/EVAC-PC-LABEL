#include "program_reader_tests.h"
#include "cache_memory.h"
#include "gpu_program.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/specialization.h"
#include "video_core/amdgpu/regs_shader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Ketsui::GpuCore {
namespace {
constexpr u64 base = 0x400000;
constexpr auto info_bytes = offsetof(AmdGpu::BinaryInfo, crc32) + sizeof(AmdGpu::BinaryInfo::crc32);
struct Memory final : CacheMemoryBackend {
    std::array<u8, 4096> bytes{};
    std::vector<std::pair<u64, u64>> readable;
    unsigned reads{}, failures{};
    std::function<void()> before_read;
    bool IsMapped(u64 address, u64 size) const noexcept override {
        if (!size || address < base || size > bytes.size() || address - base > bytes.size() - size) { return false; }
        return std::ranges::any_of(readable, [&](auto range) { return address >= range.first && address - range.first <= range.second && size <= range.second - (address - range.first); });
    }
    u64 MappedBytes(u64, u64) const noexcept override { return 0; }
    void ReadProgram(u64 address, void* output, u64 size) override {
        ++reads;
        if (before_read) { auto operation = std::move(before_read); operation(); }
        if (!IsMapped(address, size)) { ++failures; throw std::runtime_error("Authored unreadable shader extent"); }
        std::memcpy(output, bytes.data() + (address - base), static_cast<std::size_t>(size));
    }
    void ReadSparse(u64, void*, u64) override { throw std::logic_error("Unexpected sparse read"); }
    void ReadMetadata(u64 address, void* output, u64 size) override { ReadProgram(address, output, size); }
    bool WriteBacking(u64, const void*, u64) override { return false; }
    void Protect(u64, u64, unsigned) override { throw std::logic_error("Unexpected protection"); }
    bool HandleAccess(u64, u64, bool, bool) override { return false; }
    bool IsGpuThread() const noexcept override { return true; }
    void RunGpuSync(std::function<void()>) override { throw std::logic_error("Unexpected worker dispatch"); }
    void Program(u32 length, u32 metadata = 512) {
        bytes.fill(0);
        for (u32 index = 0; index < length / 4; ++index) { const u32 word = 0x11000000 + index; std::memcpy(bytes.data() + index * 4, &word, 4); }
        const std::array<u32, 2> header{0xbeeb03ffu, metadata / 8 - 1};
        std::memcpy(bytes.data(), header.data(), sizeof(header));
        AmdGpu::BinaryInfo info{}; info.signature = AmdGpu::BinaryInfo::signature_ref; info.version = 7; info.length = length; info.shader_hash = 73; info.type = 1;
        std::memcpy(bytes.data() + metadata, &info, info_bytes);
        readable = {{base, length}, {base + metadata, info_bytes}};
    }
    AmdGpu::BinaryInfo Info(u32 metadata = 512) const { AmdGpu::BinaryInfo info{}; std::memcpy(&info, bytes.data() + metadata, info_bytes); return info; }
    void Info(const AmdGpu::BinaryInfo& info, u32 metadata = 512) { std::memcpy(bytes.data() + metadata, &info, info_bytes); }
};
}
unsigned TestProgramReader() {
    unsigned checks{};
    const auto check = [&](bool okay, const char* message) { if (!okay) { throw std::runtime_error(message); } ++checks; };
    const auto rejects = [](auto operation) { try { operation(); } catch (const std::exception&) { return true; } return false; };
    Memory memory; memory.Program(64); CacheMemoryScope memory_scope(memory);
    std::array<u32, 16> user{};
    check(rejects([&] { (void)ReadProgram(base, user); }), "Program read without a store was accepted");
    ProgramStore store; ProgramScope program_scope(store);
    const auto first = ReadProgram(base, user);
    check(memory.reads == 3 && first.code.size() == 16 && first.guest_base == base, "Cold program did not retain bounded three-read semantics");
    const auto first_word = first.code[2];
    const auto scratch = store.read_scratch.data(); const auto capacity = store.read_scratch.capacity();
    bool hot_stable = true;
    for (unsigned i = 0; i < 100; ++i) { const auto next = ReadProgram(base, user); hot_stable &= next.hash == first.hash && next.code.data() == first.code.data(); }
    check(hot_stable, "Identical live shader was not deduplicated");
    check(memory.reads == 203 && store.read_scratch.data() == scratch && store.read_scratch.capacity() == capacity, "Hot program did not use two fresh reads and reusable scratch");
    const u32 changed = first_word ^ 0x01000000; std::memcpy(memory.bytes.data() + 8, &changed, 4);
    const auto second = ReadProgram(base, user);
    check(second.hash != first.hash && second.code[2] == changed && first.code[2] == first_word, "Same-address code write was stale or damaged owned prior code");
    auto info = memory.Info(); info.shader_hash = 74; memory.Info(info);
    const auto new_metadata = ReadProgram(base, user);
    check(new_metadata.hash != second.hash, "Current shader metadata was not reread");
    memory.Program(96); const auto before_growth = memory.reads; const auto grown = ReadProgram(base, user);
    check(grown.code.size() == 24 && memory.reads == before_growth + 3, "Growing live code did not trigger a bounded full reread");
    memory.Program(16); const auto before_shrink = memory.reads; const auto failures = memory.failures;
    const auto shrunk = ReadProgram(base, user);
    check(shrunk.code.size() == 4 && memory.reads == before_shrink + 4 && memory.failures == failures + 1 && !store.scratch_leased, "Shortened/remapped code did not fall back safely");
    const auto before_hot = memory.reads; (void)ReadProgram(base, user);
    check(memory.reads == before_hot + 2, "Read extent was not updated after remapping");
    memory.Program(16, 1024); const auto moved = ReadProgram(base, user);
    check(moved.code.size() == 4 && store.read_extents.at(base).code_bytes == 16, "Moving metadata was not followed through the current header");
    memory.Program(64); (void)ReadProgram(base, user);
    info = memory.Info(); info.signature[0] ^= 1; memory.Info(info);
    check(rejects([&] { (void)ReadProgram(base, user); }) && !store.scratch_leased, "Bad live metadata was accepted or leaked the scratch lease");
    memory.Program(64); info = memory.Info(); info.version = 6; memory.Info(info);
    check(rejects([&] { (void)ReadProgram(base, user); }), "Unsupported live metadata version was accepted");
    memory.Program(64); info = memory.Info(); info.length = 5; memory.Info(info);
    check(rejects([&] { (void)ReadProgram(base, user); }), "Misaligned/short code extent was accepted");
    memory.Program(64); const u32 invalid = 0; std::memcpy(memory.bytes.data(), &invalid, 4);
    check(rejects([&] { (void)ReadProgram(base, user); }), "Changed invalid header was hidden by read-extent reuse");
    memory.Program(64); const auto reads = memory.reads;
    check(rejects([&] { (void)ReadProgram(base + 1, user); }) && rejects([&] { (void)ReadProgram((std::numeric_limits<u64>::max)() - 255, user); }) && memory.reads == reads, "Invalid shader addresses reached the memory reader");
    info = memory.Info(); info.length = 516; memory.Info(info);
    check(rejects([&] { (void)ReadProgram(base, user); }), "Code extending past declared metadata was accepted");
    memory.Program(64); const u32 distant = (std::numeric_limits<u32>::max)(); std::memcpy(memory.bytes.data() + 4, &distant, 4);
    check(rejects([&] { (void)ReadProgram(base, user); }), "Unbounded metadata offset was accepted");
    memory.Program(64); memory.readable.pop_back();
    check(rejects([&] { (void)ReadProgram(base, user); }) && !store.scratch_leased, "Unreadable metadata was replaced with a stale snapshot");
    memory.Program(64); const auto unchanged = ReadProgram(base, user); user[3] = 97;
    check(ReadProgram(base, user).user_data[3] == 97 && ReadProgram(base, user).hash == unchanged.hash, "Current draw user data was hidden by code reuse");
    auto nested = unchanged; bool nested_called{};
    memory.before_read = [&] { nested_called = true; nested = ReadProgram(base, user); };
    const auto outer = ReadProgram(base, user);
    check(nested_called && nested.hash == outer.hash && nested.code.data() == outer.code.data() && !store.scratch_leased, "Reentrant read damaged the outer reusable scratch");
    const auto owned = ReadProgram(base, user); store.programs.at(owned.hash)[2] ^= 1;
    check(rejects([&] { (void)ReadProgram(base, user); }), "Content collision checking was skipped");
    {
        auto& packed = memory; packed.Program(64, 64); packed.readable = {{base, 64 + info_bytes}};
        ProgramStore packed_store; ProgramScope packed_scope(packed_store);
        const auto initial = ReadProgram(base, user); const auto before = packed.reads;
        check(ReadProgram(base, user).hash == initial.hash && packed.reads == before + 1,
              "Contiguous shader code and metadata should be reread together");
        auto live = packed.Info(64); ++live.shader_hash; packed.Info(live, 64);
        const auto changed_metadata = ReadProgram(base, user);
        check(changed_metadata.hash != initial.hash, "Combined read hid a same-address metadata mutation");
        const u32 instruction = initial.code[2] ^ 1; std::memcpy(packed.bytes.data() + 8, &instruction, 4);
        check(ReadProgram(base, user).code[2] == instruction && initial.code[2] != instruction,
              "Combined read hid a live code mutation or altered prior owned code");
        live.version = 6; packed.Info(live, 64);
        check(rejects([&] { (void)ReadProgram(base, user); }) && !packed_store.scratch_leased,
              "Combined read accepted invalid current metadata");
        packed.Program(96, 96); packed.readable = {{base, 96 + info_bytes}};
        check(ReadProgram(base, user).code.size() == 24, "Combined hint hid growth or relocated metadata");
        const auto grown_reads = packed.reads;
        check(ReadProgram(base, user).code.size() == 24 && packed.reads == grown_reads + 1,
              "Combined hint did not follow the new live extent");
        packed.Program(16, 16); const auto packed_failures = packed.failures;
        check(ReadProgram(base, user).code.size() == 4 && packed.failures == packed_failures + 1,
              "Combined hint did not fall back after shrinking or splitting the mapping");
        const auto split_reads = packed.reads;
        check(ReadProgram(base, user).code.size() == 4 && packed.reads == split_reads + 2 && packed.failures == packed_failures + 1,
              "Failed combined hint should retain fresh split reads without repeated speculative faults");
    }
    // Fetch programs have no declared extent. Remember only a successful read
    // length, never bytes: every subsequent draw must still see current code.
    auto& fetch_memory = memory;
    fetch_memory.reads = fetch_memory.failures = 0;
    const std::array<u32, 1> placeholder{0xbf810000u};
    std::array<u32, 16> fetch_user{};
    const u64 fetch_address = base + 128;
    std::memcpy(fetch_user.data(), &fetch_address, sizeof(fetch_address));
    Shader::Info fetch_info{Shader::HwStage::Vertex, Shader::SwStage::Vertex,
                           Shader::ShaderParams{fetch_user, placeholder, 1}};
    const auto publish_fetch = [&](std::initializer_list<u32> words) {
        std::memcpy(fetch_memory.bytes.data() + 128, words.begin(), words.size() * 4);
        fetch_memory.readable = {{fetch_address, words.size() * 4}};
    };
    publish_fetch({0xbf8c0000u, 0xbf8c0000u, 0xe00c2000u, 0u,
                   0xbf8c0000u, 0xbe802000u});
    const auto first_fetch = Shader::Gcn::GetFetchShaderCode(fetch_info, 0);
    check(first_fetch.size() == 6 && fetch_memory.reads == 6,
          "Cold fetch reader must stop at its exact live return");
    const auto before_fetch_hot = fetch_memory.reads;
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0) == first_fetch &&
          fetch_memory.reads == before_fetch_hot + 1,
          "Hot fetch must reread the entire last extent in one bounded read");
    publish_fetch({0xbf8c0001u, 0xbf8c0000u, 0xe00c2000u, 0u,
                   0xbf8c0000u, 0xbe802000u});
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0).front() == 0xbf8c0001u &&
          first_fetch.front() == 0xbf8c0000u,
          "Same-address fetch mutation must be visible without altering prior owned code");
    publish_fetch({0xbf8c0000u, 0xbf8c0000u, 0xe00c2000u, 0u,
                   0xbf8c0000u, 0xbf8c0000u, 0xbe802000u});
    const auto before_fetch_growth = fetch_memory.reads;
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0).size() == 7 &&
          fetch_memory.reads == before_fetch_growth + 2,
          "Growing fetch must scan beyond the old extent to its new return");
    publish_fetch({0xbe802000u});
    const auto fetch_failures = fetch_memory.failures;
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0).size() == 1 &&
          fetch_memory.failures == fetch_failures + 1,
          "Shortened fetch mapping must safely fall back from the previous extent");
    publish_fetch({0xbe800000u});
    check(rejects([&] { Shader::Gcn::GetFetchShaderCode(fetch_info, 0); }),
          "Live unsupported fetch scalar instruction must not be hidden by an extent hint");
    publish_fetch({0xe00c2000u});
    check(rejects([&] { Shader::Gcn::GetFetchShaderCode(fetch_info, 0); }),
          "Live two-word fetch with unmapped second word must reject");
    publish_fetch({0xbf8c0000u, 0xbe802000u});
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0).size() == 2,
          "Fetch read recovers after an invalid prior program");
    publish_fetch({0xbe802000u, 0xbf8c0000u});
    check(Shader::Gcn::GetFetchShaderCode(fetch_info, 0).size() == 1,
          "Earlier live return must truncate the prior extent even if its tail stays mapped");
    fetch_info.has_fetch_shader = true;
    fetch_info.fetch_shader_sgpr_base = 0;
    publish_fetch({0xc0820300u, 0xbf8c0000u, 0xe00c2000u, 0x00010000u, 0xbe802000u});
    const u64 descriptor_address = base + 512;
    std::memcpy(fetch_user.data() + 2, &descriptor_address, 8);
    fetch_memory.readable.push_back({descriptor_address, 16});
    std::array<u32, 4> descriptor{static_cast<u32>(base + 1024), 16u << 16, 1, 0x00077fac};
    std::memcpy(fetch_memory.bytes.data() + 512, descriptor.data(), 16);
    Shader::RuntimeInfo runtime{};
    runtime.Initialize(Shader::HwStage::Vertex, Shader::SwStage::Vertex);
    fetch_info.RefreshFlatBuf();
    const Shader::StageSpecialization first_spec(fetch_info, runtime, {}, {});
    check(first_spec.vs_attribs.size() == 1 && fetch_info.fetch_buffers_valid &&
          fetch_info.fetch_buffers.size() == 1 && fetch_info.fetch_buffers[0].base_address == base + 1024,
          "Vertex specialization must publish its current descriptor snapshot");
    const auto before_repeat = fetch_memory.reads;
    const Shader::StageSpecialization repeated_spec(fetch_info, runtime, {}, {});
    check(first_spec == repeated_spec && fetch_memory.reads >= before_repeat + 2,
          "Decoded fetch reuse must still read live code and descriptors each draw");
    descriptor[0] += 64;
    descriptor[3] = (descriptor[3] & ~(7u << 12)) | (5u << 12);
    std::memcpy(fetch_memory.bytes.data() + 512, descriptor.data(), 16);
    fetch_info.RefreshFlatBuf();
    check(!fetch_info.fetch_buffers_valid, "A new shader-input refresh must retire the prior descriptor snapshot");
    const Shader::StageSpecialization changed_spec(fetch_info, runtime, {}, {});
    check(first_spec.vs_attribs != changed_spec.vs_attribs &&
          fetch_info.fetch_buffers[0].base_address == base + 1088,
          "Same-address descriptor format and address changes must affect specialization and binding inputs");
    publish_fetch({0xc0820300u, 0xbf8c0000u, 0xe00c2000u, 0x00010400u, 0xbe802000u});
    Shader::Gcn::FetchShaderData decoded;
    check(Shader::Gcn::ParseFetchShader(fetch_info, decoded) && decoded.attributes[0].dest_vgpr == 4,
          "Decoded fetch cache must observe same-address instruction rewrites");
    publish_fetch({0xbe800000u});
    check(rejects([&] { Shader::Gcn::ParseFetchShader(fetch_info, decoded); }),
          "A cached fetch must not hide newly invalid live instructions");
    publish_fetch({0xbe802000u});
    check(Shader::Gcn::ParseFetchShader(fetch_info, decoded) && decoded.attributes.empty() && decoded.size == 4,
          "A shortened valid fetch must drop all prior decoded attributes");
    return checks;
}
}
