#include "daioujou_arrange.h"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace cave::ps4 {
namespace {
struct MotionFrame {
    std::string_view sha256;
    std::size_t icon, visibility_mask;
    unsigned existing_badge;
};

constexpr std::string_view kDirectSha = "5efa4c72cc172075ed212650c8b489d17ddd5b4a020de165de3d60889c8d1898";
constexpr std::array kPause{
    MotionFrame{"7f162f754bbc267f03ec0d1c1d570fccd73cf499f1b8a893e9e41c6021fbf4d9", 0x1ceb, 0, 83},
    MotionFrame{"b41bd58e33a67f95c928df20dc871efed7ef0568e7c7375072176897ca0e2fa6", 0x1d01, 0, 79},
};
constexpr std::array kOptions{
    MotionFrame{"4055148beeb8b22124c1867ed658154c976ca53556dd59df9f2e1db3811fd1c1", 0x482a, 0x482d, 0},
    MotionFrame{"ab32a4ecca83941ba15eb8ef6216c1d1a9cadf0dad5f5ee57f27d8d9b4ffe017", 0x4863, 0x4866, 0},
};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

unsigned Number(std::span<const std::uint8_t> bytes, std::size_t at, unsigned width) {
    Require(width && width <= 4 && at <= bytes.size() && width <= bytes.size() - at,
        "Invalid DaiOuJou motion field");
    unsigned value{};
    for (unsigned i = 0; i < width; ++i) value |= unsigned(bytes[at + i]) << (8 * i);
    return value;
}

void SetNumber(std::span<std::uint8_t> bytes, std::size_t at, unsigned width, unsigned value) {
    (void)Number(bytes, at, width);
    Require(width == 4 || value < (1u << (8 * width)), "X Arrange motion field does not fit");
    for (unsigned i = 0; i < width; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

unsigned MotionHeaderChecksum(std::span<const std::uint8_t> motion) {
    unsigned a = 1, b = 0;
    for (std::size_t at = 8; at < 56; ++at) {
        if (at >= 40 && at < 44) continue;
        a = (a + motion[at]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

unsigned AppendBadgeName(std::vector<std::uint8_t>& motion) {
    const auto table = Number(motion, 16, 4), strings = Number(motion, 20, 4), suffix = Number(motion, 44, 4);
    Require(table >= 56 && table < strings && strings <= suffix && suffix <= motion.size() &&
        Number(motion, 40, 4) == MotionHeaderChecksum(motion), "Unexpected DaiOuJou motion header");
    const auto count_width = Number(motion, table, 1) - 12;
    const auto count = Number(motion, table + 1, count_width);
    const auto item_width = Number(motion, table + 1 + count_width, 1) - 12;
    Require(item_width && item_width <= 4 && count <= 4096 &&
        table + 2 + count_width + count * item_width == strings, "Unexpected DaiOuJou motion string table");
    constexpr std::array<std::uint8_t, 7> name{'b','g','m','_','0','3',0};
    // Keep every chunk's alignment and offset within the chunk data unchanged.
    constexpr unsigned shift = 32;
    motion.reserve(motion.size() + shift);
    SetNumber(motion, table + 1, count_width, count + 1);
    motion.insert(motion.begin() + strings, item_width, 0);
    SetNumber(motion, strings, item_width, suffix - strings);
    auto label = motion.insert(motion.begin() + suffix + item_width, shift - item_width, 0);
    std::ranges::copy(name, label);
    SetNumber(motion, 20, 4, strings + item_width);
    for (const auto at : {24, 28, 32, 44, 48, 52}) {
        const auto start = Number(motion, at, 4);
        Require(start >= suffix, "Unexpected DaiOuJou motion chunk placement");
        SetNumber(motion, at, 4, start + shift);
    }
    SetNumber(motion, 40, 4, MotionHeaderChecksum(motion));
    return count;
}

void SetDirectMenuBadge(std::vector<std::uint8_t>& motion) {
    Require(PreparationHash(motion) == kDirectSha,
        "DaiOuJou Direct Menu animation differs from its verified source");
    const auto badge = AppendBadgeName(motion);

    // The unused frame's content occupies 40 bytes. Reuse the following
    // frame's identical motion and source values, leaving room for zx/zy.
    // The old 132x28 badges and the scaled X Arrange badge share their origin.
    constexpr std::size_t content = 0x3dae;
    constexpr std::size_t values = content + 19;
    constexpr std::size_t next_motion = 0x3dfa, next_source = 0x3e06;
    constexpr std::array<std::uint8_t, 35> compact{
        33, 13, 6, 13, 100, 115, 132, 156, 172, 173, // icon, mask, motion, src, zx, zy
        13, 6, 13, 0, 2, next_motion - values, next_source - values, 6, 11,
        21, 0,               // bgm_03 string index
        7, 0x60, 0, 0x08,   // source, zx and zy
        30, 0, 0, 0, 0,     // zx
        30, 0, 0, 0, 0,     // zy
    };
    std::ranges::copy(compact, motion.begin() + content);
    SetNumber(motion, values + 1, 1, badge);
    SetNumber(motion, values + 7, 4, std::bit_cast<unsigned>(132.0f / 184.0f));
    SetNumber(motion, values + 12, 4, std::bit_cast<unsigned>(28.0f / 40.0f));
}
}

void ApplyDaioujouArrangeMotion(std::vector<std::uint8_t>& motion, DaioujouArrangeMotion kind, bool update) {
    if (kind == DaioujouArrangeMotion::Direct) {
        SetDirectMenuBadge(motion);
        return;
    }
    const auto& frame = kind == DaioujouArrangeMotion::Pause ? kPause[update] : kOptions[update];
    Require(PreparationHash(motion) == frame.sha256,
        "DaiOuJou BGM menu animation differs from its verified source");
    const auto width = Number(motion, frame.icon, 1) - 20;
    const auto badge = frame.existing_badge ? frame.existing_badge : AppendBadgeName(motion);
    SetNumber(motion, frame.icon + 1, width, badge);
    if (frame.visibility_mask) {
        Require(Number(motion, frame.visibility_mask, 4) == 0x08040007,
            "Unexpected DaiOuJou BGM menu opacity field");
        // The disabled frame has an explicit opacity of zero. Remove the
        // opacity override so the badge inherits the same opacity as its peers.
        SetNumber(motion, frame.visibility_mask + 1, 3, 0x080000);
    }
}
}
