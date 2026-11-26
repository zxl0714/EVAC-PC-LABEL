#include "runtime/ps2_memory.h"
#include "state_archive.h"
#include <algorithm>
#include <array>
#include <utility>

namespace {
using cave::ps2::StateReader;
using cave::ps2::StateWriter;

template<class Archive, class Registers>
void Vif(Archive& ar, Registers& vif) {
    ar.Fields(vif.stat, vif.fbrst, vif.err, vif.mark, vif.cycle, vif.mode, vif.num, vif.mask,
              vif.code, vif.itops, vif.base, vif.ofst, vif.tops, vif.itop, vif.top);
    for (auto& value : vif.row) ar.Field(value);
    for (auto& value : vif.col) ar.Field(value);
}

void Mmio(StateWriter& ar, const std::unordered_map<uint32_t, uint32_t>& registers) {
    std::vector<std::pair<uint32_t, uint32_t>> ordered(registers.begin(), registers.end());
    std::sort(ordered.begin(), ordered.end());
    ar.Items(ordered, UINT32_MAX, 8, [&](const auto& entry) { ar.Fields(entry.first, entry.second); });
}
void Mmio(StateReader& ar, std::unordered_map<uint32_t, uint32_t>& registers) {
    for (auto n = ar.Count(UINT32_MAX, 8); n; --n) {
        uint32_t address{}, value{};
        ar.Fields(address, value);
        if (!registers.emplace(address, value).second) throw std::invalid_argument("Repeated EE IO register");
    }
}

void CodeRegion(StateWriter& ar, const PS2Memory::CodeRegion& region) {
    ar.Fields(region.start, region.end);
    std::vector<uint8_t> bits((region.modified.size() + 7) / 8);
    for (size_t i = 0; i < region.modified.size(); ++i)
        if (region.modified[i]) bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    ar.Bytes(bits);
}
void CodeRegion(StateReader& ar, PS2Memory::CodeRegion& region) {
    ar.Fields(region.start, region.end);
    if (region.end <= region.start) throw std::invalid_argument("Invalid EE code region");
    const size_t words = (uint64_t(region.end) - region.start + 3) / 4;
    const auto bits = ar.Take((words + 7) / 8);
    region.modified.resize(words);
    for (size_t i = 0; i < words; ++i) region.modified[i] = (bits[i / 8] >> (i % 8)) & 1u;
}

template<class Archive, class Control>
void TransferControl(Archive& ar, Control& state) {
    ar.Fields(state.m_seenGifCopy, state.m_gifFqcPending, state.m_path3Masked,
              state.m_vif1PendingDirectQwc, state.m_vif1PendingPath2ImageQwc, state.m_vif1PendingPath2DirectHl);
    Mmio(ar, state.m_ioRegisters);
    Vif(ar, state.vif0_regs);
    Vif(ar, state.vif1_regs);
    for (auto& dma : state.dma_regs)
        ar.Fields(dma.chcr, dma.madr, dma.qwc, dma.tadr, dma.asr0, dma.asr1, dma.sadr);
    ar.Items(state.m_tlbEntries, 48, 13, [&](auto& t) { ar.Fields(t.vpn, t.pfn, t.mask, t.valid); });
    if (state.m_tlbEntries.size() != 48) throw std::invalid_argument("Invalid EE TLB length");
    ar.Blob(state.m_vif1PendingCommandData);
    ar.Items(state.m_path3MaskedFifo, UINT32_MAX, 4, [&](auto& packet) { ar.Blob(packet); });
    const auto transfer = [&](auto& entry) {
        ar.Fields(entry.fromScratchpad, entry.srcAddr, entry.qwc);
        ar.Blob(entry.chainData);
    };
    ar.Items(state.m_pendingGifTransfers, UINT32_MAX, 13, transfer);
    ar.Items(state.m_pendingVif0Transfers, UINT32_MAX, 13, transfer);
    ar.Items(state.m_pendingVif1Transfers, UINT32_MAX, 13, transfer);
    ar.Items(state.m_completedDmacCauses, UINT32_MAX, 4, [&](auto& cause) {
        ar.Field(cause);
        if (cause >= 10) throw std::invalid_argument("Invalid EE DMA completion");
    });
    ar.Items(state.m_codeRegions, UINT32_MAX, 9, [&](auto& r) { CodeRegion(ar, r); });
    for (auto& timer : state.m_eeTimers)
        ar.Fields(timer.count, timer.mode, timer.compare, timer.hold, timer.clockRemainder);
    ar.Fields(state.m_eeTimerPendingCycles, state.m_eeTimerInterruptDeadline);
    if (state.m_eeTimerPendingCycles > state.m_eeTimerInterruptDeadline)
        throw std::invalid_argument("Invalid EE timer deadline");
}

// CSR and the field counter are shared with the scanout thread.
constexpr std::array DisplayFields{
    &GSRegisters::pmode, &GSRegisters::smode1, &GSRegisters::smode2, &GSRegisters::srfsh,
    &GSRegisters::synch1, &GSRegisters::synch2, &GSRegisters::syncv, &GSRegisters::dispfb1,
    &GSRegisters::display1, &GSRegisters::dispfb2, &GSRegisters::display2, &GSRegisters::extbuf,
    &GSRegisters::extdata, &GSRegisters::extwrite, &GSRegisters::bgcolor, &GSRegisters::imr,
    &GSRegisters::busdir, &GSRegisters::siglblid
};
std::array<std::span<uint8_t>, 8> MemoryBanks(PS2Memory& memory) {
    if (!memory.m_ramSize || memory.m_ramSize > UINT32_MAX) throw std::logic_error("EE RAM unavailable");
    return {{{memory.m_rdram, memory.m_ramSize}, {memory.m_scratchpad, PS2_SCRATCHPAD_SIZE},
             {memory.iop_ram, 2u << 20}, {memory.m_gsVRAM, PS2_GS_VRAM_SIZE},
             {memory.m_vu0Code, PS2_VU0_CODE_SIZE}, {memory.m_vu0Data, PS2_VU0_DATA_SIZE},
             {memory.m_vu1Code, PS2_VU1_CODE_SIZE}, {memory.m_vu1Data, PS2_VU1_DATA_SIZE}}};
}
}

// The EE and GS workers park before either operation. Keep the original buffers:
// compiled code, VU executors and the graphics frontend retain their addresses.
std::vector<uint8_t> PS2Memory::saveState() {
    auto banks = MemoryBanks(*this);
    size_t capacity = 4096;
    for (const auto bank : banks) capacity += bank.size();
    std::vector<uint8_t> bytes;
    bytes.reserve(capacity);
    StateWriter writer(bytes);
    writer.Fields(uint32_t{1}, static_cast<uint32_t>(m_ramSize));
    for (auto field : DisplayFields) writer.Field(gs_regs.*field);
    writer.Fields(gs_regs.csr.load(), gs_regs.vsyncTick.load());
    {
        std::lock_guard lock(m_completedDmacMutex);
        TransferControl(writer, std::as_const(static_cast<PS2MemoryControl&>(*this)));
    }
    for (const auto bank : banks) writer.Bytes(bank);
    return bytes;
}

void PS2Memory::loadState(std::span<const uint8_t> state) {
    auto banks = MemoryBanks(*this);
    StateReader reader(state);
    uint32_t version{}, ramSize{};
    reader.Fields(version, ramSize);
    if (version != 1 || ramSize != m_ramSize) throw std::invalid_argument("Incompatible EE memory state");
    std::array<uint64_t, DisplayFields.size()> display{};
    for (auto& value : display) reader.Field(value);
    uint64_t csr{}, vsyncTick{};
    reader.Fields(csr, vsyncTick);
    PS2MemoryControl next;
    TransferControl(reader, next);
    std::array<std::span<const uint8_t>, 8> source;
    for (size_t i = 0; i < banks.size(); ++i) source[i] = reader.Take(banks[i].size());
    reader.Finish();
    {
        std::lock_guard lock(m_completedDmacMutex);
        static_assert(std::is_nothrow_move_assignable_v<PS2MemoryControl>);
        static_cast<PS2MemoryControl&>(*this) = std::move(next);
    }
    for (size_t i = 0; i < display.size(); ++i) gs_regs.*DisplayFields[i] = display[i];
    gs_regs.csr.store(csr);
    gs_regs.vsyncTick.store(vsyncTick);
    for (size_t i = 0; i < banks.size(); ++i)
        std::memcpy(banks[i].data(), source[i].data(), banks[i].size());
    markVU0CodeModified();
    markVU1CodeModified();
}
