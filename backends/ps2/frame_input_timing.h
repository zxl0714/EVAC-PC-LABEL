#pragma once

#include "ps2_runtime.h"
#include "native_host.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <stdexcept>

namespace cave::ps2 {
// Only a read followed by the selected wait in the same field earns slack
// for the next read. A slow update or a change of loop starts at full budget.
template<class Game> struct FrameInputTiming {
    using Function = PS2Runtime::RecompiledFunction;
    static inline Function readPad{}, waitForField{};
    static inline uint64_t sampleTick{};
    static inline int remainingAtRead{};
    static constexpr int fullFieldUs = 16667;
    static inline int reserveUs = fullFieldUs;
    static inline bool sampled{};

    static void Read(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        if (getRegU32(ctx, 31) != Game::readReturn || getRegU32(ctx, 4) || getRegU32(ctx, 5)) {
            readPad(memory, ctx, runtime);
            return;
        }
        if (!Game::Eligible(memory)) {
            sampled = false;
            reserveUs = fullFieldUs;
            readPad(memory, ctx, runtime);
            return;
        }
        auto& clock = runtime->eeScheduler();
        if (sampled || clock.currentVSyncTick() != sampleTick + 1) reserveUs = fullFieldUs;
        clock.deferBeforeVBlank(reserveUs, [memory,runtime](R5900Context& resumed) {
            auto& clock = runtime->eeScheduler();
            sampleTick = clock.currentVSyncTick();
            const auto budget=std::clamp<int64_t>(clock.microsecondsBeforeVBlank()-(reserveUs-Game::marginUs+1000),0,500);
            runtime->nativeHost().BeforeInput(sampleTick+Game::responseFields,static_cast<unsigned>(budget));
            remainingAtRead = static_cast<int>(clock.microsecondsBeforeVBlank());
            sampled = true;
            readPad(memory, &resumed, runtime);
        });
    }

    static void Wait(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        if (sampled && getRegU32(ctx, 31) == Game::waitReturn) {
            const auto& clock = runtime->eeScheduler();
            const auto spare = clock.microsecondsBeforeVBlank();
            if (sampleTick != clock.currentVSyncTick() || spare <= 0) {
                reserveUs = fullFieldUs;
            } else {
                if constexpr(requires { Game::DrawComplete(*runtime); })
                    Game::DrawComplete(*runtime);
                const int needed = std::clamp(remainingAtRead - static_cast<int>(spare) + Game::marginUs,
                                              Game::marginUs, fullFieldUs);
                // Keep the measured workload and its safety margin. After a
                // missed field, move a third of the *observed excess* back
                // before the read instead of taking ~50 fields to recover.
                const int reclaim = std::max(250, (reserveUs - needed) / 3);
                reserveUs = std::max(needed, reserveUs - reclaim);
            }
            sampled = false;
        }
        waitForField(memory, ctx, runtime);
    }

    static void Bind(PS2Runtime& runtime) {
        sampleTick = 0;
        sampled = false;
        reserveUs = fullFieldUs;
        readPad = runtime.lookupFunction(Game::pad);
        waitForField = runtime.lookupFunction(Game::wait);
        if (!readPad || !waitForField || !runtime.replaceFunction(Game::pad, &Read) ||
            !runtime.replaceFunction(Game::wait, &Wait))
            throw std::runtime_error("Cannot bind PS2 frame input");
    }
};
}
