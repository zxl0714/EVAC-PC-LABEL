#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sdoj::diagnostics {
struct AudioQueueLayout {
    std::uint32_t slots{}, read{}, write{}, busy{}, hold{};
};
constexpr AudioQueueLayout AudioQueue(unsigned module) {
    // Independently reviewed disc modules; the second header is four bytes shorter.
    return module == 1 ? AudioQueueLayout{0x886B1630,0x886B160C,0x886AED34,0x886B161C,0x886B1930}
         : module == 2 ? AudioQueueLayout{0x886BB0B8,0x886BB098,0x886B87C0,0x886BB0A8,0x886BB3BC}
         : AudioQueueLayout{};
}
inline std::optional<unsigned> AudioSlot(unsigned module, std::uint32_t address) {
    const auto base = AudioQueue(module).slots;
    if (!base || address < base || address - base >= 128 * 6 || (address - base) % 6) return {};
    return (address - base) / 6;
}
struct AudioCommand {
    std::uint16_t cue{};
    std::uint8_t kind{}, volume{}, priority{}, channel{};
    bool operator==(const AudioCommand&) const = default;
};
inline std::optional<AudioCommand> DecodeAudioCommand(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 6) return {};
    return AudioCommand{std::uint16_t((std::uint16_t(bytes[0]) << 8) | bytes[1]),
                        bytes[2],bytes[3],bytes[4],bytes[5]};
}
struct AudioEnqueued {
    AudioCommand command{};
    std::uint64_t us{}, serial{};
};
inline std::optional<std::uint64_t> AudioQueueAge(const AudioEnqueued& prior,
                                                 const AudioCommand& current, std::uint64_t now) {
    if (!prior.serial || !prior.us || prior.command != current || now < prior.us) return {};
    return now - prior.us;
}
struct AudioEngineCommand {
    // Original 44-byte command, not a C++ layout over guest memory. The final
    // two words are retained as raw data: their semantics are not established.
    std::array<std::uint32_t,11> words{};
    bool operator==(const AudioEngineCommand&) const = default;
};
struct AudioDuration {
    std::uint64_t count{}, total_us{}, max_us{}, over_5ms{}, over_10ms{};
    void Add(std::uint64_t begin, std::uint64_t end) {
        if (!begin || end < begin) return;
        const auto us = end - begin;
        ++count; total_us += us;
        if (us > max_us) max_us = us;
        over_5ms += us > 5000; over_10ms += us > 10000;
    }
};
// One original worker on one host thread. No locks or guest-memory accesses.
// Wait includes scheduling/preemption; it is not a measurement of CPU work.
struct AudioWorkerTiming {
    AudioDuration work, bank, wait, gap;
    std::uint64_t window_us{}, loop_us{}, bank_us{}, wait_us{}, previous_loop_us{}, invalid{};
    void Start(std::uint64_t now) {
        if (!window_us) window_us = now;
        if (loop_us || bank_us || wait_us) ++invalid;
        gap.Add(previous_loop_us, now);
        previous_loop_us = loop_us = now;
        bank_us = wait_us = 0;
    }
    void BankBegin(std::uint64_t now) { bank_us = loop_us ? now : 0; }
    void BankEnd(std::uint64_t now) {
        if (bank_us) bank.Add(bank_us, now); else ++invalid;
        bank_us = 0;
    }
    void WaitBegin(std::uint64_t now) {
        if (loop_us) work.Add(loop_us, now); else ++invalid;
        loop_us = bank_us = 0; wait_us = now;
    }
    void WaitEnd(std::uint64_t now) {
        if (wait_us) wait.Add(wait_us, now); else ++invalid;
        wait_us = 0;
    }
    bool Ready(std::uint64_t now) const {
        return window_us && now >= window_us && now - window_us >= 1000000;
    }
    void Roll(std::uint64_t now) {
        work = {}; bank = {}; wait = {}; gap = {}; invalid = 0;
        window_us = now; // Keep previous_loop_us across summary windows.
    }
};
inline std::optional<AudioEngineCommand> DecodeAudioEngineCommand(std::span<const std::uint8_t> bytes) {
    if (bytes.size()!=44) return {};
    AudioEngineCommand value;
    for(unsigned n=0;n<11;++n) {
        const auto p=bytes.subspan(n*4,4);
        value.words[n]=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|
                       (std::uint32_t(p[2])<<8)|p[3];
    }
    return value;
}
}
