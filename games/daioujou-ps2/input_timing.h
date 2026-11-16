#pragma once
#include "backends/ps2/frame_input_timing.h"

namespace cave::daioujou_ps2 {
// Both programs sample P1 after the display flip. Their last draw finishes
// before sceGsSyncV, which selects the completed back buffer at the next blank.
template<uint32_t ReadReturn, uint32_t WaitReturn>
struct FrameLoop {
    static constexpr int marginUs = 2600;
    static constexpr unsigned responseFields = 1;
    static constexpr uint32_t pad = 0x10c790, readReturn = ReadReturn;
    static constexpr uint32_t wait = 0x100d70, waitReturn = WaitReturn;
    static bool Eligible(const uint8_t*) { return true; }
    static void DrawComplete(PS2Runtime& runtime) {
        runtime.nativeHost().SimulationReady(runtime.eeScheduler().currentVSyncTick() + responseFields);
    }
};

inline void BindInputTiming(PS2Runtime& runtime, std::string_view program) {
    if (program == "boot.elf") {
        runtime.eeScheduler().setScanoutAfterDisplayFlip(true);
        cave::ps2::FrameInputTiming<FrameLoop<0x2176b0, 0x21673c>>::Bind(runtime);
    } else if (program == "death.elf") {
        runtime.eeScheduler().setScanoutAfterDisplayFlip(true);
        cave::ps2::FrameInputTiming<FrameLoop<0x1b0e88, 0x1aff24>>::Bind(runtime);
    }
}
}
