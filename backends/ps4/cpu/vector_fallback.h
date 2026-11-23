#pragma once
#include <Zydis/Zydis.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <windows.h>

namespace cave::ps4::cpu {

// Guest register state at entry to the SSE2 bridge. The assembly offsets are
// checked here. Each Windows thread owns the software high halves of its YMMs.
struct VectorFrame {
    std::uint64_t gpr[16];
    std::uint64_t flags;
    const void *resume;
    std::uint8_t xmm[16][16];
};
static_assert(offsetof(VectorFrame, flags) == 128 && offsetof(VectorFrame, resume) == 136 &&
              offsetof(VectorFrame, xmm) == 144 && sizeof(VectorFrame) == 400);

struct VectorOperand {
    enum Kind : std::uint8_t { Vector, General, Memory, Immediate };
    Kind kind{};
    std::uint8_t index{}, bytes{};
};
struct VectorOp {
    ZydisMnemonic mnemonic{};
    std::array<VectorOperand, 4> operands{};
    std::uint8_t count{}, width{}, scale{}, address_width{};
    std::int8_t base{-1}, index{-1};
    std::uint64_t displacement{};
    std::uintptr_t pc{};
    const void *resume{}; // only set on the last operation of a contiguous run
};
bool DecodeVector(const ZydisDecodedInstruction &, const ZydisDecodedOperand *, std::uintptr_t, VectorOp &);
extern "C" void Ps4VectorBridge();
extern "C" void Ps4ExecuteVectorRun(VectorFrame *, const VectorOp *);
class VectorFallback {
  public:
    VectorFallback();
    ~VectorFallback();
    // Called while the guest image is writable. A function is never joined to its neighbour.
    std::size_t Prepare(std::span<std::uint8_t> function);
    bool Redirect(EXCEPTION_POINTERS *) const;

  private:
    void Publish();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool ExecuteUnlistedVector(CONTEXT &, std::span<const std::uint8_t>);
} // namespace cave::ps4::cpu
