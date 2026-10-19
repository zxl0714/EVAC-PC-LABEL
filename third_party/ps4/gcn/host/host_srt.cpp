#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/ir/passes/ir_passes.h"

namespace Shader::Optimization {
namespace {
void Source(const IR::Value& value, u32& index, u32& constant) {
    index = UINT32_MAX; constant = 0;
    if (value.IsImmediate()) { constant = value.U32(); return; }
    const auto* inst = value.TryInst();
    if (!inst || inst->GetOpcode() != IR::Opcode::GetUserData) { throw std::runtime_error("GCN nested or computed SRT pointer is not implemented"); }
    index = static_cast<u32>(inst->Arg(0).ScalarReg());
    if (index >= 16) { throw std::runtime_error("GCN SRT user-data index is out of range"); }
}
}
void FlattenExtendedUserdataPass(IR::Program& program) {
    auto& info = program.info;
    info.srt_info.host_reads.clear();
    u32 offset = 16;
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::ReadConstBuffer) {
                // Ordinary scalar constant-buffer loads remain GPU loads.
                // Only reads feeding another resource descriptor need a CPU
                // SRT traversal; that nested case needs its own host plan.
                if (inst.Flags<IR::BufferInstInfo>().sharp_source) { throw std::runtime_error("GCN nested constant-buffer SRT adapter is not implemented"); }
                continue;
            }
            if (inst.GetOpcode() != IR::Opcode::ReadConst) { continue; }
            const auto* base = inst.Arg(0).TryInst();
            if (!base || base->GetOpcode() != IR::Opcode::CompositeConstructU32x2 || !inst.Arg(1).IsImmediate() || offset >= 4096) {
                throw std::runtime_error("GCN SRT address expression is outside the current host adapter");
            }
            PersistentSrtInfo::HostRead read{};
            Source(base->Arg(0), read.low_ud, read.low_constant);
            Source(base->Arg(1), read.high_ud, read.high_constant);
            read.offset_dw = inst.Arg(1).U32(); read.destination_dw = offset;
            info.srt_info.host_reads.push_back(read);
            inst.SetFlags(static_cast<u16>(offset++));
        }
    }
    info.srt_info.flattened_bufsize_dw = offset;
    info.RefreshFlatBuf();
}
}
