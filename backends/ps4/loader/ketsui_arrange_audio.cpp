#include "ketsui_arrange_audio.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Bytes = std::span<std::uint8_t>;
struct Sites {
    std::string_view fingerprint;
    std::size_t setter, direct_play, direct_info, event_play, event_fade;
};
constexpr std::array kSites{
    Sites{"944428bba1318003014d95bcdb4743c08f6a8e803f6648f6b7b0354780df61bd",
        0x6db0, 0x20dd0, 0x20e40, 0x28c9c, 0x28d13},
    Sites{"d12e8fd86939f736e652492798def72d340730c0ad0add994f6dc0ff0c51b825",
        0x6dc0, 0x20f60, 0x20fd0, 0x28f5c, 0x28fd3},
};
constexpr std::size_t kStubSize = 48;
constexpr std::size_t kRowSize = 12;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Expect(Bytes bytes, std::size_t at, std::initializer_list<std::uint8_t> expected) {
    Require(at <= bytes.size() && expected.size() <= bytes.size() - at &&
        std::equal(expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at)),
        "Ketsui BGM instructions differ from the verified core");
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[at + i]) << (8 * i);
    return value;
}

void Write32(Bytes bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint64_t Target(Bytes code, std::size_t operand, bool short_branch) {
    const auto offset = short_branch ? std::bit_cast<std::int8_t>(code[operand]) :
        std::bit_cast<std::int32_t>(Read32(code, operand));
    const auto address = static_cast<std::int64_t>(operand) + (short_branch ? 1 : 4) + offset;
    Require(address >= 0 && static_cast<std::uint64_t>(address) < code.size(), "Ketsui BGM branch leaves the original text");
    return static_cast<std::uint64_t>(address);
}

void Relative(Bytes bytes, std::size_t at, std::uint64_t origin, std::uint64_t destination) {
    const auto offset = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(origin + at + 4);
    Require(offset >= INT32_MIN && offset <= INT32_MAX, "Ketsui BGM branch is out of range");
    Write32(bytes, at, static_cast<std::uint32_t>(offset));
}

void Jump(Bytes text, std::size_t at, std::size_t replaced, std::uint64_t stub) {
    text[at] = 0xe9;
    Relative(text, at + 1, 0, stub);
    std::ranges::fill(text.subspan(at + 5, replaced - 5), 0x90);
}

void PatchPlay(Bytes text, Bytes stub, std::uint64_t origin, std::uint64_t table, std::size_t at,
              std::size_t lookup, std::uint8_t mode_register, std::uint8_t cue_compare, bool event) {
    // All paths reach the original PCM row reader. Replay the original range
    // test unchanged for modes 0..4.
    const auto compare = static_cast<std::uint8_t>(mode_register | 0x30);
    Expect(text, at, {0xff, mode_register, 0x83, compare, 0x03,
        static_cast<std::uint8_t>(event ? 0x0f : 0x77)});
    if (event) Expect(text, at + 6, {0x87});
    const auto previous_fallback = Target(text, at + (event ? 7 : 6), !event);
    const auto previous_switch = at + (event ? 11 : 7);
    Expect(text, previous_switch, {0x48, 0x8d});
    Expect(text, lookup + 2, {0x48, 0x8d, 0x0c, 0x40, 0x48, 0x8d, 0x04, 0x8a});
    const std::array<std::uint8_t, 44> body{
        0xff, mode_register, 0x83, compare, 0x04,
        0x75, 0x15,                         // jne previous switch
        0x83, cue_compare, 0x0c,             // cue must be 0..12
        0x0f, 0x87, 0, 0, 0, 0,             // ja previous fallback
        0x48, 0x8d, 0x15, 0, 0, 0, 0,       // lea rdx, [table]
        0xe9, 0, 0, 0, 0,                   // jmp lookup
        0x83, compare, 0x03,
        0x0f, 0x87, 0, 0, 0, 0,             // ja previous fallback
        0xe9, 0, 0, 0, 0,                   // jmp previous switch
        0x90, 0x90,
    };
    std::ranges::copy(body, stub.begin());
    Relative(stub, 12, origin, previous_fallback);
    Relative(stub, 19, origin, table);
    Relative(stub, 24, origin, lookup);
    Relative(stub, 33, origin, previous_fallback);
    Relative(stub, 38, origin, previous_switch);
    Jump(text, at, previous_switch - at, origin);
}

void PatchFade(Bytes text, Bytes stub, std::uint64_t origin, std::uint64_t table, std::size_t at) {
    Expect(text, at, {0xff, 0xcb, 0x45, 0x31, 0xf6, 0x83, 0xfb, 0x03, 0x77, 0x19});
    const auto previous_fallback = Target(text, at + 9, true);
    Expect(text, previous_fallback - 5, {0x44, 0x8b, 0x74, 0x81, 0x08});
    const std::array<std::uint8_t, 43> body{
        0xff, 0xcb, 0x45, 0x31, 0xf6,       // dec ebx; xor r14d, r14d
        0x83, 0xfb, 0x04, 0x75, 0x13,      // original switch unless mode 5
        0x83, 0xf8, 0x0c, 0x77, 0x0e,      // cue must be 0..12
        0x48, 0x8d, 0x0d, 0, 0, 0, 0,      // lea rcx, [table]
        0xe9, 0, 0, 0, 0,                  // jmp original row calculation
        0x90, 0x90,
        0x83, 0xfb, 0x03, 0x0f, 0x87, 0, 0, 0, 0,
        0xe9, 0, 0, 0, 0,
    };
    std::ranges::copy(body, stub.begin());
    Relative(stub, 18, origin, table);
    Relative(stub, 23, origin, previous_fallback - 9);
    Relative(stub, 34, origin, previous_fallback);
    Relative(stub, 39, origin, at + 10);
    Jump(text, at, 10, origin);
}
}

bool HasKetsuiArrangeAudio(std::string_view fingerprint) {
    return std::ranges::find(kSites, fingerprint, &Sites::fingerprint) != kSites.end();
}

std::vector<std::uint8_t> PrepareKetsuiArrangeAudio(
    std::string_view fingerprint, Bytes text, Bytes attributes, std::span<const std::uint8_t> data,
    std::uint64_t extension_address) {
    const auto sites = std::ranges::find(kSites, fingerprint, &Sites::fingerprint);
    Require(sites != kSites.end(), "Unsupported Ketsui X Arrange core");
    const auto at = sites->setter;
    // The generic attribute dispatcher clamps x-bgm before calling its setter.
    Expect(attributes, 0x20, {0,0,0,0,4,0,0,0,0xd0,0x68,0,0,0,0,0,0});
    const auto name=Read32(attributes,0x18);
    Expect(text,name,{'x','-','b','g','m',0});
    attributes[0x24]=5;
    Expect(text, at, {0x48, 0x85, 0xff, 0x74, 0x17, 0x83, 0xfe, 0x04, 0x77, 0x12, 0x89, 0xf0, 0x48, 0x8d, 0x0d});
    const auto previous_modes = Target(text, at + 15, false);
    Require(previous_modes + 20 <= text.size(), "Ketsui BGM mode table is incomplete");
    for (unsigned i = 0; i < 5; ++i) {
        Require(Read32(text, previous_modes + 4 * i) == (i < 2 ? 1 - i : i), "Unexpected Ketsui BGM mode order");
    }

    constexpr auto pcm_offset = 4 * kStubSize;
    constexpr auto modes_offset = pcm_offset + kKetsuiArrangeCueCount * kRowSize;
    constexpr auto stereo_offset = 0x7c570 - 0x50000;
    Require(data.size() >= stereo_offset + kKetsuiArrangeCueCount * kRowSize, "Ketsui BGM table is incomplete");
    std::vector<std::uint8_t> extension(modes_offset + 24, 0x90);
    Bytes output = extension;
    for (unsigned cue = 0; cue < kKetsuiArrangeCueCount; ++cue) {
        const auto row = data.subspan(stereo_offset + cue * kRowSize, kRowSize);
        Require(Read32(row, 0) == 0x100 + cue, "Unexpected Ketsui stereo cue");
        std::ranges::copy(row, output.subspan(pcm_offset + cue * kRowSize).begin());
        Write32(output, pcm_offset + cue * kRowSize, kKetsuiArrangeFirstPcm + cue);
    }
    std::ranges::copy(text.subspan(previous_modes, 20), output.subspan(modes_offset).begin());
    Write32(output, modes_offset + 20, 5);
    text[at + 7] = 5;
    Relative(text, at + 15, 0, extension_address + modes_offset);
    const auto table = extension_address + pcm_offset;
    PatchPlay(text, output.first(kStubSize), extension_address, table, sites->direct_play,
        sites->direct_play + 0x3f, 0xca, 0xf9, false);
    PatchPlay(text, output.subspan(kStubSize, kStubSize), extension_address + kStubSize, table,
        sites->direct_info, sites->direct_info + 0x3e, 0xcf, 0xfe, false);
    PatchPlay(text, output.subspan(2 * kStubSize, kStubSize), extension_address + 2 * kStubSize, table,
        sites->event_play, sites->event_play + 0x173, 0xcb, 0xf8, true);
    PatchFade(text, output.subspan(3 * kStubSize, kStubSize), extension_address + 3 * kStubSize, table, sites->event_fade);
    return extension;
}
}
