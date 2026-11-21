#include "game_overrides.h"
#include "backends/ps2/sdk_binding.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <stdexcept>

namespace {
void WaitGraphicsField(uint8_t*, R5900Context*, PS2Runtime* runtime) {
    // The disc's sceGsSyncV reads the interlace flag from its SDK GS parameters.
    // The movie requests the opposite field before it starts uploading frames.
    constexpr uint32_t gsParameters = 0x00218970u;
    const bool interlaced = runtime->memory().read16(gsParameters) == 1u;
    auto& scheduler = runtime->eeScheduler();
    if (interlaced) {
        scheduler.waitVSync(scheduler.currentVSyncTick(), -1, [](R5900Context& resumed) {
            // The scheduler's parity result denotes the field that just ended.
            SET_GPR_U32(&resumed, 2, getRegU32(&resumed, 2) ^ 1u);
        });
    }
    scheduler.waitVSync(scheduler.currentVSyncTick(), 1);
}

#ifdef EVAC_PS2_RTYPE_FINAL_MOVIES
void DemuxMovie(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    const auto available = getRegU32(context, 6);
    // The disc callback at 0x157720 declines a whole packet when its ring
    // cannot accept it. Leave input with the caller until the game drains it.
    constexpr uint32_t movieWorkspacePointer = 0x002c7180u;
    constexpr uint32_t audioRingOffset = 0x00138170u;
    const uint32_t workspace = runtime->memory().read32(movieWorkspacePointer);
    if (available && workspace) {
        const uint32_t ring = workspace + audioRingOffset;
        const uint32_t capacity = runtime->memory().read32(ring + 0x3cu);
        const uint32_t used = runtime->memory().read32(ring + 0x38u);
        const uint32_t free = capacity - std::min(capacity, used);
        // All ten PSS streams use two-sector audio packets. A packet started
        // in a previous call may become deliverable with just one new byte.
        constexpr uint32_t maxPacketBytes = 2u * 2048u;
        uint32_t budget = free > maxPacketBytes ? free - maxPacketBytes : 0u;
        // Once fewer than five file bytes remain, the disc's playback loop
        // leaves the audio pump and waits only for video. Keep the last byte
        // of sector padding and the four-byte end code until the pump has
        // transferred every complete stereo block to the IOP.
        constexpr uint32_t playbackLoopReturn = 0x00157190u;
        constexpr uint32_t endBytes = 5u;
        constexpr uint32_t stereoBlock = 1024u;
        if (getRegU32(context, 31) == playbackLoopReturn && runtime->memory().read32(ring) == 2u) {
            const uint32_t remaining = getRegU32(context, 23);
            if (remaining > endBytes) {
                budget = std::min(budget, remaining - endBytes);
            } else if (remaining == endBytes && used >= stereoBlock) {
                budget = 0u;
            }
        }
        if (capacity) SET_GPR_U32(context, 6, std::min(available, budget));
    }
    ps2_stubs::sceMpegDemuxPssRing(memory, context, runtime);
    SET_GPR_U32(context, 6, available);
    if (available != 0u && getRegU32(context, 2) == 0u) {
        // Give the audio and video consumers a field to free their buffers.
        auto& scheduler = runtime->eeScheduler();
        scheduler.waitVSync(scheduler.currentVSyncTick(), 0);
    }
}

void RotateReadyQueue(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    constexpr uint32_t retryFullMovieQueue = 0x00159338u;
    constexpr uint32_t waitForDecoderFlush = 0x00157290u;
    constexpr uint32_t waitForLastPicture = 0x001572b4u;
    const auto caller = getRegU32(context, 31);
    if (caller == retryFullMovieQueue || caller == waitForDecoderFlush || caller == waitForLastPicture) {
        // The display interrupt releases a picture buffer on each field.
        // Spinning here while the last pictures drain stalls host audio.
        auto& scheduler = runtime->eeScheduler();
        scheduler.waitVSync(scheduler.currentVSyncTick(), 0);
    }
    ps2_syscalls::RotateThreadReadyQueue(memory, context, runtime);
}

void RotateMovieWorker(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    constexpr uint32_t movieSettings = 0x002c7150u;
    const auto config = runtime->memory().read32(movieSettings);
    SET_GPR_U32(context, 4, runtime->memory().read16(config));
    RotateReadyQueue(memory, context, runtime);
}

void RegisterMovieStream(uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    constexpr uint32_t videoStream = 0u;
    if (getRegU32(context, 5) == videoStream) {
        // The host decoder receives the video payload directly from the demuxer.
        // The original video callback would also fill the unused hardware IPU ring.
        SET_GPR_U32(context, 2, 0u);
        return;
    }
    ps2_stubs::sceMpegAddStrCallback(memory, context, runtime);
}
#endif

void BindSdkServices(PS2Runtime& runtime) {
    constexpr struct {uint32_t address;const char* name;} services[] = {
        {0x00124128u, "sceCdDiskReady"},
        {0x00124810u, "sceCdStInit"},
        {0x00124840u, "sceCdStStart"},
        {0x00124878u, "sceCdStSeekF"},
        {0x001248a8u, "sceCdStSeek"},
        {0x001248d8u, "sceCdStStop"},
        {0x00124910u, "sceCdStRead"},
        {0x00124a98u, "sceCdStPause"},
        {0x00124ae8u, "sceCdStResume"},
        {0x00124b40u, "sceCdStStat"},
#ifdef EVAC_PS2_RTYPE_FINAL_MOVIES
        {0x00116a40u, "sceMpegAddBs"},
        {0x00116a68u, "sceMpegGetPicture"},
        {0x00116b60u, "sceMpegGetDecodeMode"},
        {0x00116b80u, "sceMpegIsEnd"},
        {0x00116c48u, "sceMpegAddCallback"},
        {0x00117278u, "sceMpegFlush"},
#endif
    };
    for (const auto& service : services) {
        if (!cave::ps2::BindSdkHandler(runtime, service.address, service.name)) {
            throw std::runtime_error(std::string("Cannot bind R-Type Final SDK service ") + service.name);
        }
    }
#ifdef EVAC_PS2_RTYPE_FINAL_MOVIES
    if (!runtime.replaceFunction(0x00115c08u, &cave::ps2::SdkEntry<&DemuxMovie>) ||
        !runtime.replaceFunction(0x00115f20u, &cave::ps2::SdkEntry<&RegisterMovieStream>) ||
        !runtime.replaceFunction(0x00156ec0u, &cave::ps2::SdkEntry<&RotateMovieWorker>) ||
        !runtime.replaceFunction(0x00118710u, &cave::ps2::SdkEntry<&RotateReadyQueue>)) {
        throw std::runtime_error("Cannot bind R-Type Final movie stream services");
    }
#endif
    if (!runtime.replaceFunction(0x0010e348u, &cave::ps2::SdkEntry<&WaitGraphicsField>) ||
        !runtime.replaceFunction(0x00123688u, &cave::ps2::SdkEntry<&cave::ps2::SearchSingleLayer>) ||
        !runtime.replaceFunction(0x0011fde0u, &cave::ps2::SdkEntry<&ps2_syscalls::sceSifLoadModuleFromIopRam>)) {
        throw std::runtime_error("Cannot bind R-Type Final SDK services");
    }
}
}

PS2_REGISTER_GAME_OVERRIDE("R-Type Final SLUS-20780 SDK services", "", 0x00100008u, 0x35daf325u, BindSdkServices)
