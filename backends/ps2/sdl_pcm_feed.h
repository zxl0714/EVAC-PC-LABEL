#pragma once
#include <SDL3/SDL_audio.h>
#include "pcm_queue.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace cave::ps2 {
// SDL requests source bytes. The SPU supplies 48 kHz signed 16-bit stereo
// regardless of the playback device.
struct SdlPcmFeed {
    using Reader = void (*)(void*, int16_t*, size_t);
    void* source;
    Reader read;
    uint64_t callbacks = 0, putFailures = 0;
    size_t largestRequest = 0;
    // Keep a full ring-sized pull together, including on large-period devices.
    std::array<int16_t, PcmQueue::CapacityFrames * 2> stereo{};

    static bool Prime(SDL_AudioStream* stream) {
        // Seed the converter before the first device period. At 48 kHz this
        // is 2.67 ms, independent of the physical device's period size.
        constexpr std::array<int16_t, 128 * 2> silence{};
        return SDL_PutAudioStreamData(stream, silence.data(), int(sizeof(silence)));
    }

    static void SDLCALL Pull(void* user, SDL_AudioStream* stream, int additional, int) {
        if (additional <= 0) return;
        auto& feed = *static_cast<SdlPcmFeed*>(user);
        size_t pending = (size_t(additional) + 3) / 4;
        ++feed.callbacks;
        feed.largestRequest = std::max(feed.largestRequest, pending);
        // The first request at 44.1 kHz omits the resampler's right padding.
        // Supply one SPU quantum of lookahead. SDL subtracts its remaining
        // queued samples from later requests, so this reserve does not grow.
        pending += 64;
        while (pending) {
            const auto frames = std::min(pending, feed.stereo.size() / 2);
            feed.read(feed.source, feed.stereo.data(), frames);
            if (!SDL_PutAudioStreamData(stream, feed.stereo.data(), int(frames * 4))) {
                ++feed.putFailures;
                return;
            }
            pending -= frames;
        }
    }
};
}
