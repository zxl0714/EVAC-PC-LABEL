#include "vector_fallback.h"
#include "assembler.h"
#include "near_memory.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cave::ps4::cpu {
namespace {
int GeneralIndex(ZydisRegister reg) {
    if (reg == ZYDIS_REGISTER_NONE)
        return -1;
    const auto full = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
    return full >= ZYDIS_REGISTER_RAX && full <= ZYDIS_REGISTER_R15 ? int(full - ZYDIS_REGISTER_RAX) : -2;
}
} // namespace
bool DecodeVector(const ZydisDecodedInstruction &ins, const ZydisDecodedOperand *operands, std::uintptr_t pc,
                  VectorOp &op) {
    if (ins.encoding != ZYDIS_INSTRUCTION_ENCODING_VEX || ins.meta.isa_set != ZYDIS_ISA_SET_AVX)
        return false;
    if (ins.operand_count_visible > op.operands.size())
        return false;
    op.mnemonic = ins.mnemonic;
    op.pc = pc;
    op.count = ins.operand_count_visible;
    op.width = static_cast<std::uint8_t>(ins.avx.vector_length / 8);
    op.address_width = ins.address_width;
    for (unsigned i = 0; i < op.count; ++i) {
        const auto &src = operands[i];
        auto &dst = op.operands[i];
        dst.bytes = static_cast<std::uint8_t>(src.size / 8);
        if (src.type == ZYDIS_OPERAND_TYPE_REGISTER) {
            const auto reg = src.reg.value;
            if (reg >= ZYDIS_REGISTER_XMM0 && reg <= ZYDIS_REGISTER_XMM15) {
                dst.kind = VectorOperand::Vector;
                dst.index = static_cast<std::uint8_t>(reg - ZYDIS_REGISTER_XMM0);
            } else if (reg >= ZYDIS_REGISTER_YMM0 && reg <= ZYDIS_REGISTER_YMM15) {
                dst.kind = VectorOperand::Vector;
                dst.index = static_cast<std::uint8_t>(reg - ZYDIS_REGISTER_YMM0);
            } else {
                const int index = GeneralIndex(reg);
                if (index < 0)
                    return false;
                dst.kind = VectorOperand::General;
                dst.index = static_cast<std::uint8_t>(index);
            }
        } else if (src.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            dst.kind = VectorOperand::Immediate;
            dst.index = static_cast<std::uint8_t>(src.imm.value.u);
        } else if (src.type == ZYDIS_OPERAND_TYPE_MEMORY) {
            dst.kind = VectorOperand::Memory;
            op.displacement = src.mem.disp.value;
            const bool relative = src.mem.base == ZYDIS_REGISTER_RIP || src.mem.base == ZYDIS_REGISTER_EIP;
            if (relative)
                op.displacement += pc + ins.length;
            op.base = static_cast<std::int8_t>(relative ? -1 : GeneralIndex(src.mem.base));
            op.index = static_cast<std::int8_t>(GeneralIndex(src.mem.index));
            op.scale = src.mem.scale;
            if (op.base < -1 || op.index < -1 || src.mem.segment == ZYDIS_REGISTER_FS ||
                src.mem.segment == ZYDIS_REGISTER_GS)
                return false;
        } else
            return false;
    }
    return true;
}

struct VectorFallback::Impl {
    static constexpr std::size_t slab_size = 65536;
    struct Slab {
        std::uint8_t *base;
        std::size_t used;
        bool writable;
    };
    struct Site {
        std::uint8_t *pc;
        unsigned length;
        VectorOp op;
    };
    std::vector<Slab> slabs;
    std::vector<std::unique_ptr<VectorOp[]>> runs;
    std::unordered_map<std::uintptr_t, std::uint8_t *> short_entries;
    ~Impl() {
        for (auto &slab : slabs)
            VirtualFree(slab.base, 0, MEM_RELEASE);
    }
    std::uint8_t *Allocate(std::uint8_t *pc, std::size_t size) {
        auto source = reinterpret_cast<std::uintptr_t>(pc);
        if (!slabs.empty()) {
            auto &slab = slabs.back();
            if (size <= slab_size - slab.used && std::llabs(reinterpret_cast<std::intptr_t>(slab.base) -
                                                            static_cast<std::intptr_t>(source)) < 0x70000000) {
                if (!slab.writable) {
                    DWORD old{};
                    if (!VirtualProtect(slab.base, slab_size, PAGE_READWRITE, &old))
                        throw std::runtime_error("Cannot write vector adapter");
                    slab.writable = true;
                }
                auto *result = slab.base + slab.used;
                slab.used += size;
                return result;
            }
        }
        auto *memory = AllocateNear(source, slab_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        slabs.push_back({memory, size, true});
        return memory;
    }
    static void Jump(std::uint8_t *from, const void *to) {
        const auto distance = reinterpret_cast<std::intptr_t>(to) - reinterpret_cast<std::intptr_t>(from + 5);
        if (distance < INT32_MIN || distance > INT32_MAX)
            throw std::runtime_error("Vector adapter out of range");
        const auto disp = static_cast<std::int32_t>(distance);
        from[0] = 0xe9;
        std::memcpy(from + 1, &disp, 4);
    }
    void Install(std::vector<Site> &sites) {
        if (sites.empty())
            return;
        const auto &last = sites.back();
        auto *leave = Allocate(last.pc, 16);
        Assembler exit(leave, 16);
        exit.Emit(ZYDIS_MNEMONIC_MOV, {Reg(ZYDIS_REGISTER_RSP), Mem(ZYDIS_REGISTER_RSP, -512, 8)});
        Jump(leave + exit.Size(), last.pc + last.length);
        auto ops = std::make_unique<VectorOp[]>(sites.size());
        for (std::size_t i = 0; i < sites.size(); ++i) {
            const auto &site = sites[i];
            ops[i] = site.op;
            auto *entry = Allocate(site.pc, 40);
            Assembler code(entry, 40);
            code.Emit(ZYDIS_MNEMONIC_LEA, {Reg(ZYDIS_REGISTER_RSP), Mem(ZYDIS_REGISTER_RSP, -128, 8)});
            code.Emit(ZYDIS_MNEMONIC_PUSH, {Reg(ZYDIS_REGISTER_RAX)});
            code.Emit(ZYDIS_MNEMONIC_MOV,
                      {Reg(ZYDIS_REGISTER_RAX), Imm(reinterpret_cast<std::uintptr_t>(&ops[i]))});
            code.Emit(ZYDIS_MNEMONIC_JMP, {Mem(ZYDIS_REGISTER_RIP, 0, 8)});
            code.Data64(reinterpret_cast<std::uintptr_t>(&Ps4VectorBridge));
            // A target in the middle of a run must still enter at its own operation.
            std::fill_n(site.pc, site.length, 0x90);
            if (site.length >= 5)
                Jump(site.pc, entry);
            else {
                site.pc[0] = 0xcc;
                short_entries.emplace(reinterpret_cast<std::uintptr_t>(site.pc), entry);
            }
        }
        ops[sites.size() - 1].resume = leave;
        runs.push_back(std::move(ops));
        sites.clear();
    }
};
VectorFallback::VectorFallback() : impl_(std::make_unique<Impl>()) {}
VectorFallback::~VectorFallback() = default;
std::size_t VectorFallback::Prepare(std::span<std::uint8_t> function) {
    ZydisDecoder decoder{};
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    std::vector<Impl::Site> sites;
    std::size_t converted{};
    for (std::size_t i = 0; i < function.size();) {
        auto *pc = function.data() + i;
        ZydisDecodedInstruction ins{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        const bool valid =
            ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, pc, function.size() - i, &ins, operands));
        VectorOp op{};
        if (valid && DecodeVector(ins, operands, reinterpret_cast<std::uintptr_t>(pc), op)) {
            sites.push_back({pc, ins.length, op});
            ++converted;
        } else
            impl_->Install(sites);
        i += valid ? ins.length : 1;
    }
    impl_->Install(sites);
    if (converted)
        Publish();
    return converted;
}
void VectorFallback::Publish() {
    for (auto it = impl_->slabs.rbegin(); it != impl_->slabs.rend() && it->writable; ++it) {
        DWORD old{};
        if (!VirtualProtect(it->base, Impl::slab_size, PAGE_EXECUTE_READ, &old) ||
            !FlushInstructionCache(GetCurrentProcess(), it->base, it->used))
            throw std::runtime_error("Cannot publish vector adapters");
        it->writable = false;
    }
}
bool VectorFallback::Redirect(EXCEPTION_POINTERS *exception) const {
    const auto it = impl_->short_entries.find(
        reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress));
    if (it == impl_->short_entries.end())
        return false;
    exception->ContextRecord->Rip = reinterpret_cast<std::uintptr_t>(it->second);
    return true;
}
} // namespace cave::ps4::cpu
