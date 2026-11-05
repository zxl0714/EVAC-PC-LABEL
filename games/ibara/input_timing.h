#pragma once
#include "../../backends/ps2/frame_input_timing.h"
#include "../../backends/ps2/native_host.h"
#include <array>
#include <cstring>

namespace cave::ibara {
// SLPM-66301: only the first pad read of the active player update and the
// matching WaitSema participate. Attract and menus retain their timing.
struct FrameLoop {
    static constexpr int marginUs = 2600;
    static constexpr unsigned responseFields = 2;
    static constexpr uint32_t pad = 0x153c48, readReturn = 0x1b5090;
    static constexpr uint32_t wait = 0x148ee0, waitReturn = 0x1b2864;
    static bool Eligible(const uint8_t* memory) {
        uint32_t scene, demo;
        std::memcpy(&scene, memory + 0x338b08, sizeof(scene));
        std::memcpy(&demo, memory + 0x338e7c, sizeof(demo));
        return scene == 5 && !demo;
    }
};
using InputTiming = cave::ps2::FrameInputTiming<FrameLoop>;

struct DisplayWorker {
    static inline PS2Runtime::RecompiledFunction sleep{};
    static void Sleep(uint8_t* ram,R5900Context* ctx,PS2Runtime* runtime) {
        // The draw worker sets its completion flag, then sleeps at 1b39d8.
        // The next sceGsSwapDBuff consumes the DISPENV selected at db + 0x244.
        if(getRegU32(ctx,31)==0x1b39e0 && FrameLoop::Eligible(ram)) {
            constexpr unsigned db=0x4f9490;
            uint32_t which;
            std::memcpy(&which,ram+db+0x244,sizeof(which));
            if(which<=1) {
                std::array<uint64_t,5> env{};
                std::memcpy(env.data(),ram+db+which*sizeof(env),sizeof(env));
                const auto& gs=runtime->memory().gs();
                if((gs.pmode&3)==2 && env[0]==gs.pmode && env[1]==gs.smode2 &&
                   env[3]==gs.display2 && env[4]==gs.bgcolor &&
                   (env[2]^gs.dispfb2) && !((env[2]^gs.dispfb2)&~0x1ffull))
                    runtime->nativeHost().OfferReadyFrame(*runtime,env[2],11500);
            }
        }
        sleep(ram,ctx,runtime);
    }
    static void Bind(PS2Runtime& runtime) {
        constexpr uint32_t address=0x148dc0;
        sleep=runtime.lookupFunction(address);
        if(!sleep || !runtime.replaceFunction(address,&Sleep))
            throw std::runtime_error("Cannot bind Ibara render completion");
    }
};
}
