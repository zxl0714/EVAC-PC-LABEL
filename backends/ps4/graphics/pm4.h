#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>

namespace kt::gfx::pm4 {
enum class Error { None, Truncated, UnsupportedType, ReservedHeader, UnsupportedOpcode, UnsupportedFlags, BadSize, BadValue, RegisterRange };
struct Packet {
    std::uint32_t offset{}, header{};
    std::uint8_t opcode{};
    bool padding{}, compute{}, predicated{};
    std::span<const std::uint32_t> body;
};
struct Decoded { Error error{}; Packet packet{}; std::uint32_t next{}; };
// No pointer following, allocation, or GPU work. All offsets are word offsets
// within the supplied span; decoding never fetches beyond it.
Decoded Decode(std::span<const std::uint32_t> words, std::uint32_t offset);

struct RegisterBank {
    std::array<std::uint32_t, 1024> values{};
    std::bitset<1024> known;
    std::optional<std::uint32_t> Get(std::uint32_t offset) const {
        return offset < values.size() && known[offset] ? std::optional(values[offset]) : std::nullopt;
    }
};
struct MemoryWait { std::uint64_t address; std::uint32_t reference, mask, interval; };
struct DmaTransfer { std::uint64_t source, destination; std::uint32_t control, command, bytes; };
// Base and size are decoded from the packet's 256-byte units, not pointers
// to dereference. The control retains the PFP/ME engine bit and cache actions.
struct MemoryAcquire { std::uint32_t control; std::uint64_t base, bytes; std::uint32_t interval; };
struct IndexedDraw { std::uint64_t address; std::uint32_t count, max_count; };
struct Analysis {
    Error error{};
    std::uint32_t error_offset{}, packets{}, padding_packets{}, register_writes{}, clears{}, acquires{}, context_controls{};
    std::optional<std::uint32_t> instances;
    std::uint32_t waits{};
    std::uint32_t draws{};
    std::optional<std::uint32_t> last_draw_vertices;
    std::optional<IndexedDraw> last_indexed_draw;
    std::optional<MemoryWait> last_wait;
    std::optional<MemoryAcquire> last_acquire;
    std::uint32_t dma_transfers{};
    std::optional<DmaTransfer> first_dma;
    std::optional<DmaTransfer> last_dma;
    RegisterBank context, shader, uconfig;
    bool Complete() const { return error == Error::None; }
};
// A syntax/state journal, NOT an execution engine. Synchronization packets are
// counted, not executed. A failed journal is partial and cannot be submitted.
// ClearState invalidates knowledge; it does not assert hardware reset = zero.
Analysis Analyze(std::span<const std::uint32_t> words);
// Advance exactly one decoded packet. Callers execute waits/draws against the
// state at this position, before advancing to subsequent register writes.
Error Advance(const Packet& packet, Analysis& state);

constexpr std::uint32_t kHardware350Words = 256;
std::uint32_t WriteHardware350(std::span<std::uint32_t> output, bool neo=false);
std::uint32_t WriteDefaultContext(std::span<std::uint32_t> output,bool neo=false);
// Use only after Advance/Analyze has validated this register packet.
std::uint32_t RegisterOffset(const Packet& packet);
bool WriteWaitFlip(std::span<std::uint32_t> output, std::uint64_t label_address);
bool WriteVertexShader(std::span<std::uint32_t> output, std::span<const std::uint32_t> registers, std::uint32_t modifier, bool update = false);
bool WritePixelShader350(std::span<std::uint32_t> output, std::span<const std::uint32_t> registers, bool update = false);
bool WriteDrawAuto(std::span<std::uint32_t> output, std::uint32_t vertices, std::uint32_t flags, bool neo=false);
bool WriteDrawIndex(std::span<std::uint32_t> output, std::uint32_t count, std::uint64_t address, std::uint32_t flags, bool neo=false);
bool SelfTest();
}
