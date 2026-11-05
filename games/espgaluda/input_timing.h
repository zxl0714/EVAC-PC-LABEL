#pragma once
#include "../../backends/ps2/frame_input_timing.h"
#include "../../backends/ps2/native_host.h"

namespace cave::espgaluda {
// SLPS-25352: the pad update calls scePadRead at 0x11e4c8. After the
// scene draws, the frame loop calls sceGsSyncPath and then sceGsSyncV.
struct FrameLoop {
    static constexpr int marginUs = 2600;
    static constexpr unsigned responseFields = 1;
    static constexpr uint32_t pad = 0x1106e8, readReturn = 0x11e4d0;
    static constexpr uint32_t wait = 0x101830, waitReturn = 0x11d4a8;
    static bool Eligible(const uint8_t*) { return true; }
    static void DrawComplete(PS2Runtime& runtime) {
        const auto& regs=runtime.memory().gs();
        if((regs.smode2&3u)==1u && regs.dispfb1==regs.dispfb2)
            runtime.nativeHost().OfferReadyFrame(runtime,regs.dispfb2,3300);
    }
};
using InputTiming = cave::ps2::FrameInputTiming<FrameLoop>;
}
