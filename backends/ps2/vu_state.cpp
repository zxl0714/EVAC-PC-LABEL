#include "vu_state.h"
#include "state_archive.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_memory.h"
#include <memory>
#include <utility>

namespace cave::ps2 {
template<class Archive, class Unit>
void VuStateArchive::Transfer(Archive& ar, Unit& vu) {
    using Interpreter = VU1Interpreter;
    auto& cpu = vu.m_state;
    const auto fields = [&](auto& values) {
        for (auto& value : values) ar.Field(value);
    };
    for (auto& vector : cpu.vf) fields(vector);
    fields(cpu.vi);
    fields(cpu.acc);
    ar.Fields(cpu.q, cpu.p, cpu.i, cpu.r, cpu.pc, cpu.mac, cpu.clip, cpu.status, cpu.cycles,
              cpu.ebit, cpu.haltAfterDelaySlot, cpu.dBitEnabled, cpu.tBitEnabled, cpu.stoppedByD,
              cpu.stoppedByT, cpu.top, cpu.itop, cpu.branchPending, cpu.branchTarget, cpu.branchDelay);
    const auto codeSize = vu.m_unit == Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
    if (cpu.pc > codeSize + 7u || cpu.branchTarget >= codeSize || cpu.branchDelay > 1)
        throw std::invalid_argument("Invalid VU program counter or branch");

    ar.Fields(vu.m_nextScalarCommit, vu.m_flagHead, vu.m_flagCount);
    if (vu.m_flagHead >= vu.m_flagPipeline.size() || vu.m_flagCount > vu.m_flagPipeline.size())
        throw std::invalid_argument("Invalid VU flags queue");
    for (auto& f : vu.m_flagPipeline)
        ar.Fields(f.readyCycle, f.issueCycle, f.mac, f.status, f.extraSticky, f.clip, f.valid,
                  f.writesMac, f.writesStatus, f.writesSticky, f.writesClip);
    const auto scalar = [&](auto& entry) {
        ar.Fields(entry.readyCycle, entry.value, entry.statusDi, entry.valid);
    };
    scalar(vu.m_fdiv);
    for (auto& entry : vu.m_efu) scalar(entry);

    auto& kick = vu.m_xgkick;
    ar.Fields(kick.sourceAddress, kick.totalBytes, kick.copiedBytes, kick.currentTagEnd,
              kick.cycleCredit, kick.issueCycle, kick.syncCycle, kick.active, kick.currentTagEop);
    if (kick.totalBytes > kick.packet.size() || kick.copiedBytes > kick.packet.size() ||
        kick.currentTagEnd > kick.packet.size() || ((kick.totalBytes | kick.copiedBytes | kick.currentTagEnd) & 15u) ||
        (kick.currentTagEnd && kick.currentTagEnd < kick.copiedBytes))
        throw std::invalid_argument("Invalid VU GIF transfer");
    ar.Bytes(std::span(kick.packet).first(kick.copiedBytes));

    for (auto& lanes : vu.m_vfReady) fields(lanes);
    fields(vu.m_vfReadyMax);
    ar.Field(vu.m_accReadyMax);
    fields(vu.m_viReady);
    fields(vu.m_accReady);
    ar.Fields(vu.m_inFlightNext, vu.m_inFlightCount, vu.m_inFlightHidden, vu.m_logInFlight);
    if (vu.m_inFlightNext >= vu.m_inFlight.size() || vu.m_inFlightCount > vu.m_inFlight.size())
        throw std::invalid_argument("Invalid VU writeback queue");
    for (auto& entry : vu.m_inFlight) {
        auto kind = static_cast<uint8_t>(entry.kind);
        ar.Fields(entry.readyCycle, kind, entry.reg, entry.lanes);
        using Kind = Interpreter::InFlightWrite::Kind;
        if (kind > static_cast<uint8_t>(Kind::Acc) || entry.lanes > 15 || entry.reg >= 32 ||
            (kind == static_cast<uint8_t>(Kind::Vi) && entry.reg >= 16))
            throw std::invalid_argument("Invalid VU writeback destination");
        if constexpr (std::is_same_v<Archive, StateReader>) entry.kind = static_cast<Kind>(kind);
        fields(entry.oldValue);
        fields(entry.newValue);
    }
    ar.Fields(vu.m_cycle, vu.m_lastWritebackCycle, vu.m_efuResourceReady, vu.m_workingClip,
              vu.m_currentUpperInstruction, vu.m_viBranchBackupValue, vu.m_viBranchBackupReg,
              vu.m_viBranchBackupValid, vu.m_stopRequested, vu.m_pendingHaltD, vu.m_pendingHaltT,
              vu.m_flagWritesDead, vu.m_deadSticky);
    if (vu.m_viBranchBackupReg >= 16) throw std::invalid_argument("Invalid VU branch register");
}

std::vector<uint8_t> VuStateArchive::Save(const VU1Interpreter& vu) {
    std::vector<uint8_t> bytes;
    StateWriter writer(bytes);
    writer.Fields(uint32_t{1}, static_cast<uint8_t>(vu.m_unit));
    Transfer(writer, vu);
    return bytes;
}

void VuStateArchive::Load(VU1Interpreter& vu, std::span<const uint8_t> state) {
    StateReader reader(state);
    uint32_t version{};
    uint8_t unit{};
    reader.Fields(version, unit);
    if (version != 1 || unit != static_cast<uint8_t>(vu.m_unit))
        throw std::invalid_argument("Incompatible VU state");
    // The executable and decoded microcode caches belong to the old timeline.
    auto next = std::make_unique<VU1Interpreter>(vu.m_unit);
    Transfer(reader, *next);
    reader.Finish();
    next->m_activeMemory = vu.m_activeMemory;
    next->m_activeGs = vu.m_activeGs;
    next->m_activeVuData = vu.m_activeVuData;
    next->m_activeVuDataSize = vu.m_activeVuDataSize;
    next->m_compiledCodeDisabled = vu.m_compiledCodeDisabled;
    next->m_packetObserver = std::move(vu.m_packetObserver);
    static_assert(std::is_nothrow_move_assignable_v<VU1Interpreter>);
    vu = std::move(*next);
}
}
