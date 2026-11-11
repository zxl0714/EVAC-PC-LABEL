#include "daioujou_arrange.h"
#include <algorithm>
#include <bit>
#include <initializer_list>
#include <stdexcept>

namespace cave::ps4 {
namespace {
struct RouteSites {
    std::string_view module;
    std::size_t setter, play, fade;
};
constexpr std::array kRoutes{
    RouteSites{"daioujou_01000.prx", 0x6dd0, 0xb625, 0x283fa},
    RouteSites{"daioujou_01010.prx", 0x6e00, 0xf4d5, 0x3012a},
    RouteSites{"daioujou_01020.prx", 0x6e00, 0xfbf5, 0x3121a},
    RouteSites{"daioujou_01030.prx", 0x6e10, 0xfc95, 0x326aa},
};
constexpr std::size_t kGameTrackCount = 13;
constexpr std::size_t kPcmRowSize = 12;
constexpr std::uint64_t kDataAddress = 0x400000;
constexpr std::size_t kStubSpacing = 64;
constexpr std::size_t kTableOffset = 2 * kStubSpacing;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t at) {
    Require(at <= bytes.size() && 4 <= bytes.size() - at, "DaiOuJou BGM read is out of range");
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[at + i]) << (8 * i);
    return value;
}

void Write32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
    Require(at <= bytes.size() && 4 <= bytes.size() - at, "DaiOuJou BGM write is out of range");
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint64_t RipTarget(std::span<const std::uint8_t> text, std::size_t at) {
    const auto target = static_cast<std::int64_t>(at + 4) + std::bit_cast<std::int32_t>(Read32(text, at));
    Require(target >= 0, "DaiOuJou BGM target is outside the image");
    return static_cast<std::uint64_t>(target);
}

void Relative(std::span<std::uint8_t> bytes, std::size_t at, std::uint64_t start, std::uint64_t target) {
    const auto displacement = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(start + at + 4);
    Require(displacement >= INT32_MIN && displacement <= INT32_MAX, "DaiOuJou BGM branch is out of range");
    Write32(bytes, at, static_cast<std::uint32_t>(displacement));
}

void Expect(std::span<const std::uint8_t> text, std::size_t at, std::initializer_list<std::uint8_t> bytes) {
    Require(at <= text.size() && bytes.size() <= text.size() - at &&
        std::equal(bytes.begin(), bytes.end(), text.begin() + at),
        "DaiOuJou BGM instructions differ from the verified module");
}

void BuildRoute(std::span<std::uint8_t> text, std::size_t site, std::size_t table_lookup,
                std::span<std::uint8_t> output, std::uint64_t origin, std::uint64_t pcm_table,
                std::uint8_t mode_register, std::uint8_t event_register, std::uint8_t sweep_jump) {
    // At the original switch, r12d/r13d holds the music mode and r15d/r14d
    // holds the request number. Only 07..13 hex have X Arrange counterparts.
    // The new branch rejoins the existing PCM table reader. The two original
    // switch instructions are replayed for every other request or mode.
    const std::array<std::uint8_t, 42> code{
        0x41, 0x83, mode_register, 0x03,    // cmp mode, 3
        0x75, 0x15,                         // jne original
        0x41, 0x8d, event_register, 0xf9,    // lea eax, [event - 7]
        0x83, 0xf8, 0x0c,                   // cmp eax, 12
        0x77, 0x0c,                         // ja original
        0x48, 0x8d, 0x0d, 0, 0, 0, 0,      // lea rcx, [rip + pcm_table]
        0xe9, 0, 0, 0, 0,                   // jmp existing PCM lookup
        0x41, 0x83, mode_register, 0x02,    // original: cmp mode, 2
        0x0f, 0x84, 0, 0, 0, 0,             // je Super Sweep
        0xe9, 0, 0, 0, 0,                   // jmp next original instruction
    };
    Expect(text, site, {0x41, 0x83, mode_register, 0x02, 0x74, sweep_jump});
    Expect(text, table_lookup, {0x48, 0x8d, 0x04, 0x40, 0x48, 0x8d, 0x04, 0x81});
    std::ranges::copy(code, output.begin());
    Relative(output, 18, origin, pcm_table);
    Relative(output, 23, origin, table_lookup);
    Relative(output, 33, origin, site + 6 + sweep_jump);
    Relative(output, 38, origin, site + 6);
    text[site] = 0xe9;
    Relative(text, site + 1, 0, origin);
    text[site + 5] = 0x90;
}
}

std::vector<std::uint8_t> PrepareDaioujouArrangeAudio(
    std::string_view module, std::span<std::uint8_t> text, std::span<const std::uint8_t> data,
    std::uint64_t original_image_size, std::uint64_t extension_address) {
    const auto selected = std::ranges::find(kRoutes, module, &RouteSites::module);
    Require(selected != kRoutes.end(), "Unsupported DaiOuJou BGM core");
    const auto [name, setter, play, fade] = *selected;
    (void)name;
    Expect(text, setter, {0x83, 0xff, 0x02, 0x77, 0x12, 0x89, 0xf8, 0x48, 0x8d, 0x0d});
    Expect(text, setter + 14, {0x8b, 0x04, 0x81, 0x89, 0x05});
    Expect(text, setter + 23, {0xc3});
    const auto modes = RipTarget(text, setter + 10);
    Require(Read32(text, modes) == 0 && Read32(text, modes + 4) == 1 &&
        Read32(text, modes + 8) == 2, "Unexpected DaiOuJou BGM mode table");
    const auto current_mode = RipTarget(text, setter + 19);
    Require(current_mode >= kDataAddress && current_mode + 4 <= original_image_size,
        "DaiOuJou BGM state is outside the original image");
    constexpr std::array<std::uint8_t, 14> set_mode{
        0x83, 0xff, 0x03, 0x77, 0x08,  // Accept modes 0..3.
        0x89, 0xf8, 0x89, 0x05, 0, 0, 0, 0, 0xc3,
    };
    std::ranges::copy(set_mode, text.begin() + setter);
    Relative(text, setter + 9, 0, current_mode);
    std::fill(text.begin() + setter + set_mode.size(), text.begin() + setter + 24, 0x90);

    Expect(text, play + 16, {0x48, 0x8d, 0x0d});
    Expect(text, fade + 22, {0x48, 0x8d, 0x0d});
    const auto stereo = RipTarget(text, play + 19);
    Require(stereo == RipTarget(text, fade + 25) && stereo >= kDataAddress &&
        stereo - kDataAddress <= data.size() && kGameTrackCount * kPcmRowSize <= data.size() - (stereo - kDataAddress),
        "Unexpected DaiOuJou stereo PCM table");
    const auto rows = data.subspan(static_cast<std::size_t>(stereo - kDataAddress), kGameTrackCount * kPcmRowSize);
    std::vector<std::uint8_t> extension(kTableOffset + rows.size(), 0x90);
    std::ranges::copy(rows, extension.begin() + kTableOffset);
    for (unsigned i = 0; i < kGameTrackCount; ++i) {
        const auto row = i * kPcmRowSize;
        Require(Read32(rows, row) == i + 7, "Unexpected DaiOuJou stereo track ID");
        Write32(extension, kTableOffset + row, kDaioujouArrangeFirstPcm + i);
    }
    const auto table = extension_address + kTableOffset;
    const std::span<std::uint8_t> stubs = extension;
    BuildRoute(text, play, play + 0x35, stubs.first(kStubSpacing),
        extension_address, table, 0xfc, 0x47, 0x16);
    BuildRoute(text, fade, fade + 0x9a, stubs.subspan(kStubSpacing, kStubSpacing),
        extension_address + kStubSpacing, table, 0xfd, 0x46, 0x7c);
    return extension;
}
}
