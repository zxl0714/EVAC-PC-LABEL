#pragma once

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace cave::ibara {
// SLPM-66301 reads both pads at the head of its loop, updates the scene, then
// calls WaitSema from 0x1b285c before flipping. Move only the observed spare
// time ahead of the first read. Menu, attract and unrelated semaphore calls
// retain their original schedule.
struct InputTiming {
    static inline PS2Runtime::RecompiledFunction readPad{}, waitSema{};
    static inline uint64_t sampleTick{};
    static inline int remainingAtRead{}, reserveUs{};
    static inline bool sampled{};

    static void Read(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        if (getRegU32(ctx, 31) != 0x1b5090 || getRegU32(ctx, 4) || getRegU32(ctx, 5)) {
            readPad(memory, ctx, runtime);
            return;
        }
        uint32_t scene, demo;
        std::memcpy(&scene, memory + 0x338b08, sizeof(scene));
        std::memcpy(&demo, memory + 0x338e7c, sizeof(demo));
        auto& scheduler = runtime->eeScheduler();
        if (scene != 5 || demo) {
            sampled = false;
            reserveUs = 16667;
            readPad(memory, ctx, runtime);
            return;
        }
        if (sampled || scheduler.currentVSyncTick() != sampleTick + 1)
            reserveUs = 16667;
        scheduler.deferBeforeVBlank(reserveUs, [memory,runtime](R5900Context& resumed) {
            auto& clock = runtime->eeScheduler();
            sampleTick = clock.currentVSyncTick();
            remainingAtRead = static_cast<int>(clock.microsecondsBeforeVBlank());
            sampled = true;
            readPad(memory, &resumed, runtime);
        });
    }

    static void Wait(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        if (sampled && getRegU32(ctx, 31) == 0x1b2864) {
            const auto& clock = runtime->eeScheduler();
            const auto spare = clock.microsecondsBeforeVBlank();
            if (sampleTick != clock.currentVSyncTick() || spare <= 0) {
                reserveUs = 16667;
            } else {
                // Leave two milliseconds for variation. Expand immediately
                // on a heavier scene, reclaim slack gradually on lighter ones.
                const int needed = remainingAtRead - static_cast<int>(spare) + 2000;
                reserveUs = std::clamp(std::max(needed, reserveUs - 250), 2000, 16667);
            }
            sampled = false;
        }
        waitSema(memory, ctx, runtime);
    }

    static void Bind(PS2Runtime& runtime) {
        sampleTick = 0;
        sampled = false;
        reserveUs = 16667;
        readPad = runtime.lookupFunction(0x153c48);
        waitSema = runtime.lookupFunction(0x148ee0);
        if (!readPad || !waitSema || !runtime.replaceFunction(0x153c48, &Read) ||
            !runtime.replaceFunction(0x148ee0, &Wait))
            throw std::runtime_error("Cannot bind Ibara frame input");
    }
};
}
