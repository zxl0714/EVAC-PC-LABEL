// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include <unordered_map>
#include <boost/container/flat_map.hpp>
#include "common/logging/log.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/opcodes.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/ir/srt_gvn_table.h"

namespace {
using namespace Shader;

struct PassInfo {
    struct PointerListCompare {
        bool operator()(const IR::Value& a, const IR::Value& b) const {
            // Keep order with the following criteria:
            // 1. If immediate, sort by immediate value
            // 2. If not immediate, sort by instruction order
            // 3. Immediates come first, then instructions
            if (a.IsImmediate() && b.IsImmediate()) {
                return a.U32() < b.U32();
            } else if (!a.IsImmediate() && !b.IsImmediate()) {
                auto a_inst = a.Inst();
                auto b_inst = b.Inst();
                if (a_inst == b_inst) { return false; }
                auto a_block = a_inst->GetParent();
                auto b_block = b_inst->GetParent();
                if (a_block != b_block) {
                    return a_block < b_block; // Sort by block address
                }

                auto it = a_block->Instructions().iterator_to(*a_inst);
                auto end = a_block->Instructions().end();
                for (; it != end; ++it) {
                    if (&*it == b_inst) {
                        return true;
                    }
                }
                return false;
            } else {
                return a.IsImmediate(); // Immediates come first
            }
        }
    };

    // map offset to inst
    using PtrUserList =
        boost::container::flat_map<IR::Value, Shader::IR::Inst*, PointerListCompare>;

    Optimization::SrtGvnTable gvn_table;
    // keys are GetUserData, ReadConst or ReadConstBuffer instructions that are used as pointers
    std::unordered_map<IR::Inst*, PtrUserList> pointer_uses;
    // GetUserData instructions corresponding to sgpr_base of SRT roots
    boost::container::small_flat_map<IR::ScalarReg, IR::Inst*, 1> srt_roots;

    // pick a single inst for a given value number
    std::unordered_map<u32, IR::Inst*> vn_to_inst;

    // Bumped during codegen to assign offsets to readconsts
    u16 dst_off_dw;

    PtrUserList* GetUsesAsPointer(IR::Inst* inst) {
        auto it = pointer_uses.find(inst);
        if (it != pointer_uses.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Return a single instruction that this instruction is identical to, according
    // to value number
    // The "original" is arbitrary. Here it's the first instruction found for a given value number
    IR::Inst* DeduplicateInstruction(IR::Inst* inst) {
        auto it = vn_to_inst.try_emplace(gvn_table.GetValueNumber(inst), inst);
        return it.first->second;
    }
};
} // namespace

namespace Shader::Optimization {

namespace {

static inline u16 GetFlatbufOffset(const IR::Inst* inst) {
    if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        auto inst_info = inst->Flags<IR::BufferInstInfo>();
        return inst_info.flatbuf_off_dw;
    }
    if (inst->GetOpcode() == IR::Opcode::ReadConst) {
        return inst->Flags<u16>();
    }
    UNREACHABLE_MSG("Instruction not supported");
}

static inline void SetFlatbufOffset(IR::Inst* inst, u16 offset) {
    if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        auto inst_info = inst->Flags<IR::BufferInstInfo>();
        inst_info.flatbuf_off_dw.Assign(offset);
        inst->SetFlags(inst_info);
        return;
    }
    if (inst->GetOpcode() == IR::Opcode::ReadConst) {
        inst->SetFlags(offset);
        return;
    }
    UNREACHABLE_MSG("Instruction not supported");
}

static bool IsAllowedOffsetInstruction(const IR::Inst* inst) {
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetUserData:
    case IR::Opcode::ReadConst:
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::IAdd32:
    case IR::Opcode::ISub32:
    case IR::Opcode::IMul32:
    case IR::Opcode::ShiftLeftLogical32:
    case IR::Opcode::ShiftRightLogical32:
    case IR::Opcode::BitwiseAnd32:
    case IR::Opcode::BitwiseOr32:
    case IR::Opcode::BitwiseXor32:
    case IR::Opcode::BitwiseNot32:
    case IR::Opcode::UMin32:
    case IR::Opcode::UMax32:
    case IR::Opcode::BitFieldUExtract:
        return true;
    default:
        return false;
    }
}


using Srt = PersistentSrtInfo;
static u32 AppendOffset(Srt& plan, Srt::Offset node) {
    if (plan.offsets.size() >= 65536) { throw std::runtime_error("SRT offset graph exceeds limit"); }
    const auto index = static_cast<u32>(plan.offsets.size());
    plan.offsets.push_back(node); return index;
}
static u32 CompileOffset(Srt& plan, PassInfo& pass, const IR::Value& value, unsigned depth = 0) {
    if (depth >= 128) { throw std::runtime_error("SRT offset expression nesting exceeds limit"); }
    if (value.IsImmediate()) { return AppendOffset(plan, {Srt::OffsetOp::Constant, value.U32()}); }
    auto* inst = value.Inst();
    if (inst->GetOpcode() == IR::Opcode::GetUserData) {
        const auto index = static_cast<u32>(inst->Arg(0).ScalarReg());
        if (index >= NUM_USER_DATA_REGS) { throw std::runtime_error("SRT offset user-data index is invalid"); }
        return AppendOffset(plan, {Srt::OffsetOp::Flat, index});
    }
    if (inst->GetOpcode() == IR::Opcode::ReadConst || inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        const auto offset = GetFlatbufOffset(pass.DeduplicateInstruction(inst));
        if (!offset) { throw std::runtime_error("SRT offset depends on an unavailable descriptor read"); }
        return AppendOffset(plan, {Srt::OffsetOp::Flat, offset});
    }
    Srt::OffsetOp op;
    switch (inst->GetOpcode()) {
    case IR::Opcode::IAdd32: op = Srt::OffsetOp::Add; break;
    case IR::Opcode::ISub32: op = Srt::OffsetOp::Subtract; break;
    case IR::Opcode::IMul32: op = Srt::OffsetOp::Multiply; break;
    case IR::Opcode::ShiftLeftLogical32: op = Srt::OffsetOp::ShiftLeft; break;
    case IR::Opcode::ShiftRightLogical32: op = Srt::OffsetOp::ShiftRight; break;
    case IR::Opcode::BitwiseAnd32: op = Srt::OffsetOp::And; break;
    case IR::Opcode::BitwiseOr32: op = Srt::OffsetOp::Or; break;
    case IR::Opcode::BitwiseXor32: op = Srt::OffsetOp::Xor; break;
    case IR::Opcode::BitwiseNot32: op = Srt::OffsetOp::Not; break;
    case IR::Opcode::UMin32: op = Srt::OffsetOp::Minimum; break;
    case IR::Opcode::UMax32: op = Srt::OffsetOp::Maximum; break;
    case IR::Opcode::BitFieldUExtract: op = Srt::OffsetOp::BitExtract; break;
    default: throw std::runtime_error("Unsupported SRT offset expression");
    }
    const auto a = CompileOffset(plan, pass, inst->Arg(0), depth + 1);
    const auto b = op == Srt::OffsetOp::Not ? 0 : CompileOffset(plan, pass, inst->Arg(1), depth + 1);
    const auto c = op == Srt::OffsetOp::BitExtract ? CompileOffset(plan, pass, inst->Arg(2), depth + 1) : 0;
    return AppendOffset(plan, {op, a, b, c});
}
static void VisitPointer(const IR::Value& offset, IR::Inst* subtree, PassInfo& pass, Srt& plan, unsigned depth) {
    if (depth >= 128) { throw std::runtime_error("SRT pointer nesting exceeds limit"); }
    auto* uses = pass.GetUsesAsPointer(subtree);
    if (!uses) { throw std::runtime_error("SRT pointer has no descriptor consumers"); }
    const auto expression = CompileOffset(plan, pass, offset);
    plan.steps.push_back({depth == 0 ? Srt::StepOp::Root : Srt::StepOp::Push, expression});
    for (const auto& [source, use] : *uses) {
        if (pass.dst_off_dw == UINT16_MAX) { throw std::runtime_error("Flattened SRT buffer exceeds limit"); }
        const auto source_expression = CompileOffset(plan, pass, source);
        plan.steps.push_back({Srt::StepOp::Read, source_expression, pass.dst_off_dw});
        SetFlatbufOffset(use, pass.dst_off_dw++);
    }
    for (const auto& [source, use] : *uses) {
        if (pass.GetUsesAsPointer(use)) { VisitPointer(source, use, pass, plan, depth + 1); }
    }
    plan.steps.push_back({Srt::StepOp::Pop});
}
static void GenerateSrtProgram(Info& info, PassInfo& pass) {
    auto& plan = info.srt_info;
    plan.offsets.clear(); plan.steps.clear();
    plan.walker_func = nullptr; plan.walker_func_size = 0;
    pass.dst_off_dw = NUM_USER_DATA_REGS;
    for (const auto& [sgpr, root] : pass.srt_roots) {
        VisitPointer(IR::Value(static_cast<u32>(sgpr)), root, pass, plan, 0);
    }
    plan.flattened_bufsize_dw = pass.dst_off_dw;
}

static bool IsReadConstSource(const IR::Value base) {
    auto* inst = base.TryInst();
    if (!inst) {
        return false;
    }
    return inst->GetOpcode() == IR::Opcode::GetUserData ||
           inst->GetOpcode() == IR::Opcode::ReadConst ||
           inst->GetOpcode() == IR::Opcode::ReadConstBuffer;
}

void SimplifyReadConstAddressAdd(IR::Inst& inst) {
    // This handles the following pattern by combining the addition with the offset
    // s_add_u32    s12, s2, <offset>
    // s_addc_u32   s13, s3, 0
    // s_load_dword s16, s[12:13], 0x0

    if (inst.GetOpcode() != IR::Opcode::ReadConst) {
        return;
    }
    IR::Inst* addr = inst.Arg(0).Inst();
    IR::Inst* hi = addr->Arg(1).TryInst();
    IR::Inst* lo = addr->Arg(0).TryInst();
    if (!hi || !lo) {
        return;
    }

    if (lo->GetOpcode() != IR::Opcode::IAdd32 || hi->GetOpcode() != IR::Opcode::IAdd32) {
        return;
    }

    IR::Value add_offset;
    IR::Value base;
    if (base = lo->Arg(0); IsReadConstSource(base)) {
        add_offset = lo->Arg(1);
    } else if (base = lo->Arg(1); IsReadConstSource(base)) {
        add_offset = lo->Arg(0);
    } else {
        return;
    }

    IR::Inst* sel = hi->Arg(1).TryInst();
    if (!sel || sel->GetOpcode() != IR::Opcode::SelectU32) {
        return;
    }

    IR::Value sel_true = sel->Arg(1);
    IR::Value sel_false = sel->Arg(2);
    if (!sel_true.IsImmediate() || !sel_false.IsImmediate() || sel_true.U32() != 1 ||
        sel_false.U32() != 0) {
        return;
    }

    addr->SetArg(0, base);
    addr->SetArg(1, hi->Arg(0));
    for (auto [user, operand] : addr->Uses()) {
        ASSERT(user->GetOpcode() == IR::Opcode::ReadConst && operand == 0);
        IR::IREmitter ir{*user->GetParent(), IR::Block::InstructionList::s_iterator_to(*user)};
        IR::U32 offset = IR::U32{user->Arg(1)};
        IR::U32 dw_add_offset =
            add_offset.IsImmediate()
                ? ir.Imm32(add_offset.U32() >> 2u)
                : IR::U32{ir.ShiftRightLogical(IR::U32{add_offset}, ir.Imm32(2u))};
        if (offset.IsImmediate() && dw_add_offset.IsImmediate()) {
            user->SetArg(1, ir.Imm32(offset.U32() + dw_add_offset.U32()));
        } else {
            user->SetArg(1, ir.IAdd(offset, dw_add_offset));
        }
    }
}

} // Anonymous namespace

void FlattenExtendedUserdataPass(IR::Program& program) {
    auto& post_order = program.post_order_blocks;
    PassInfo pass_info;

    // traverse at end and assign offsets to duplicate readconsts, using
    // vn_to_inst as the source
    boost::container::small_vector<IR::Inst*, 32> all_readconsts, visited;
    std::queue<IR::Inst*> queue;

    for (auto it = post_order.rbegin(); it != post_order.rend(); it++) {
        IR::Block* block{*it};
        for (IR::Inst& inst : *block) {
            if (inst.GetOpcode() != IR::Opcode::ReadConst &&
                inst.GetOpcode() != IR::Opcode::ReadConstBuffer) {
                continue;
            }

            if (inst.GetOpcode() == IR::Opcode::ReadConstBuffer) {
                // Only flatten ReadConstBuffer if it was marked as a sharp source in the
                // resource discovery pass
                auto inst_info = inst.Flags<IR::BufferInstInfo>();
                if (!inst_info.sharp_source) {
                    continue;
                }
            }

            SimplifyReadConstAddressAdd(inst);

            all_readconsts.push_back(&inst);

            auto offset = inst.Arg(1);
            if (offset.IsImmediate()) {
                continue;
            }

            queue.push(offset.Inst());
            while (!queue.empty()) {
                IR::Inst* inst{queue.front()};
                queue.pop();

                if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
                    auto buffer_inst_info = inst->Flags<IR::BufferInstInfo>();
                    if (!buffer_inst_info.sharp_source.Value()) {
                        all_readconsts.push_back(inst);
                        auto offset = inst->Arg(1);
                        if (!offset.IsImmediate()) {
                            auto arg_inst = offset.Inst();
                            if (std::ranges::find(visited, arg_inst) == visited.end()) {
                                visited.push_back(arg_inst);
                                queue.push(arg_inst);
                            }
                        }
                    }
                    continue;
                }
                if (inst->GetOpcode() == IR::Opcode::GetUserData ||
                    inst->GetOpcode() == IR::Opcode::ReadConst ||
                    !IsAllowedOffsetInstruction(inst)) {
                    continue;
                }
                for (size_t arg = inst->NumArgs(); arg--;) {
                    const IR::Value arg_value = inst->Arg(arg);
                    if (arg_value.IsImmediate()) {
                        continue;
                    }
                    IR::Inst* arg_inst = arg_value.Inst();
                    if (std::ranges::find(visited, arg_inst) == visited.end()) {
                        visited.push_back(arg_inst);
                        queue.push(arg_inst);
                    }
                }
            }
        }
    }

    for (auto inst : all_readconsts) {
        if (pass_info.DeduplicateInstruction(inst) != inst) {
            // This is a duplicate of a readconst we've already visited
            continue;
        }

        IR::Inst* base = inst->Arg(0).Inst();
        if (auto* inst = base->Arg(0).TryInst();
            inst && inst->GetOpcode() == IR::Opcode::ReadFirstLane) {
            continue;
        }
        ASSERT_MSG(IsReadConstSource(base->Arg(0)), "ReadConst base low not from constant memory");
        ASSERT_MSG(IsReadConstSource(base->Arg(1)), "ReadConst base high not from constant memory");

        IR::Inst* ptr_lo = base->Arg(0).Inst();
        ptr_lo = pass_info.DeduplicateInstruction(ptr_lo);

        auto ptr_uses_kv = pass_info.pointer_uses.try_emplace(ptr_lo, PassInfo::PtrUserList{});
        PassInfo::PtrUserList& user_list = ptr_uses_kv.first->second;

        user_list[inst->Arg(1)] = inst;

        if (ptr_lo->GetOpcode() == IR::Opcode::GetUserData) {
            IR::ScalarReg ud_reg = ptr_lo->Arg(0).ScalarReg();
            pass_info.srt_roots[ud_reg] = ptr_lo;
        }
    }

    GenerateSrtProgram(program.info, pass_info);

    // Assign offsets to duplicate readconsts
    for (IR::Inst* readconst : all_readconsts) {
        ASSERT(pass_info.vn_to_inst.contains(pass_info.gvn_table.GetValueNumber(readconst)));
        IR::Inst* original = pass_info.DeduplicateInstruction(readconst);
        SetFlatbufOffset(readconst, GetFlatbufOffset(original));
    }

    program.info.RefreshFlatBuf();
}

} // namespace Shader::Optimization
