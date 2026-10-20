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
