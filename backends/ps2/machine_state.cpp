#include "machine_state.h"
#include "state_archive.h"
#include "cd_state.h"
#include "sdk_state.h"
#include "gs_completion_mailbox.h"
#include "gs_device.h"
#include "vu_state.h"
#include "../../third_party/ps2/runtime/ps2_iop_host.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "ps2x/iop/iop_subsystem.h"

namespace cave::ps2 {
namespace {
enum Part : size_t { Scheduler, Memory, Vu0, Vu1, Gif, Services, Sdk, Disc, Files, Card, Iop, Drawing, Signals, Movie, Count };
using Parts = std::array<std::span<const uint8_t>, Count>;
constexpr uint32_t Version = 2;

Parts Unpack(std::span<const uint8_t> bytes) {
    StateReader reader(bytes);
    uint32_t version{};
    reader.Field(version);
    if (version != 1 && version != Version) throw std::invalid_argument("Unsupported PS2 machine state version");
    Parts parts{};
    for (auto& part : std::span(parts).first(version == 1 ? Movie : Count)) {
        part = reader.Blob();
        if (part.empty()) throw std::invalid_argument("Missing PS2 machine state component");
    }
    reader.Finish();
    return parts;
}

PS2VfsMounts Mounts() {
    const auto& p = PS2Runtime::getIoPaths();
    return {p.hostRoot, p.cdRoot, p.mcRoot};
}
}

std::vector<uint8_t> MachineState::Save(PS2Runtime& r, const GraphicsOwner& graphics) {
    // Scheduler serialization checks that execution stopped at a resumable
    // dispatch boundary. VIF DMA is synchronous with that same EE producer.
    if (!r.eeScheduler().isExecutorThread() || r.eeScheduler().isExecutingGuest())
        throw std::logic_error("Machine state requires a parked EE dispatcher");
    std::vector<uint8_t> drawing, signals;
    graphics([&](GsDevice& gs) {
        drawing = gs.SaveDrawingState();
        signals = gs.Completions()->SaveState();
    });
    // Draining the GS can post one final status event to the EE.
    std::vector<uint8_t> bytes;
    bytes.reserve(48u << 20);
    StateWriter out(bytes);
    out.Field(Version);
    out.Blob(r.eeScheduler().saveState());
    out.Blob(r.memory().saveState());
    out.Blob(VuStateArchive::Save(r.vu0()));
    out.Blob(VuStateArchive::Save(r.vu1()));
    out.Blob(r.gifArbiter().saveState());
    out.Blob(r.saveEeServices());
    out.Blob(ps2_stubs::saveSdkState());
    out.Blob(ps2_stubs::saveCdState());
    out.Blob(r.vfs().saveState(Mounts()));
    out.Blob(ps2_stubs::saveMemoryCardState());
    r.m_iopHost->withAudioLocked([&] { out.Blob(r.m_iopSubsystem->saveState()); });
    out.Blob(drawing);
    out.Blob(signals);
    out.Blob(ps2_stubs::saveMpegState());
    return bytes;
}

void MachineState::Load(PS2Runtime& r, const GraphicsOwner& graphics, std::span<const uint8_t> bytes) {
    const auto parts = Unpack(bytes);
    const auto previous = Save(r, graphics);
    auto replace = [&](const Parts& p) {
        r.vfs().loadState(p[Files], Mounts(), r.romDevice());
        ps2_stubs::loadMemoryCardState(p[Card]);
        ps2_stubs::loadCdState(p[Disc]);
        if (p[Movie].empty()) ps2_stubs::resetMpegStubState();
        else ps2_stubs::loadMpegState(p[Movie]);
        r.memory().loadState(p[Memory]);
        r.loadEeServices(p[Services]);
        ps2_stubs::loadSdkState(p[Sdk]);
        VuStateArchive::Load(r.vu0(), p[Vu0]);
        VuStateArchive::Load(r.vu1(), p[Vu1]);
        r.gifArbiter().loadState(p[Gif]);
        r.m_iopHost->withAudioLocked([&] { r.m_iopSubsystem->loadState(p[Iop]); });
        graphics([&](GsDevice& gs) {
            gs.LoadDrawingState(p[Drawing]);
            gs.Completions()->LoadState(p[Signals]);
            gs.SetCommandGeneration(0);
        });
        // Reinstates the saved event queue, including the status event that
        // belonged to the saved mailbox, after the host notifier has run.
        r.eeScheduler().loadState(p[Scheduler]);
    };
    try {
        replace(parts);
    } catch (...) {
        const auto failure = std::current_exception();
        try { replace(Unpack(previous)); }
        catch (...) { std::throw_with_nested(std::runtime_error("Could not roll back PS2 machine state")); }
        std::rethrow_exception(failure);
    }
}

void MachineState::PauseAudio(PS2Runtime& r, bool paused, bool clearQueued) {
    r.m_iopHost->setMenuAudioPaused(paused, clearQueued);
}
}
