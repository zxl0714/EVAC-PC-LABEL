#include "diagnostics/input_trace_policy.h"
#include "diagnostics/render_trace_policy.h"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sdoj::diagnostics;
    unsigned passed{};
    auto check = [&](bool result, const char* name) {
        if (!result) throw std::runtime_error(name);
        ++passed;
    };
    const std::array<std::uint8_t, 16> bytes{
        0x12,0x34,0x56,0x78,0x20,0x01,0x80,0xff,
        0x80,0x00,0x7f,0xff,0,1,0xff,0xfe};
    const auto sample = DecodePad(bytes, 0);
    check(sample && sample->packet == 0x12345678 && sample->buttons == 0x2001,
          "guest endianness and exact button mask");
    check(sample->left_trigger == 128 && sample->right_trigger == 255,
          "trigger bytes are not word-swapped");
    check(sample->axes == std::array<std::uint16_t,4>{0x8000,0x7fff,1,0xfffe},
          "signed axis bit patterns remain exact");
    check(!DecodePad(bytes, 1167), "failed poll cannot reuse stale output");
    check(!DecodePad(std::span(bytes).first(15), 0), "short state rejected");
    check(!DecodePad({}, 0), "missing state rejected");
    auto changed = *sample; ++changed.packet;
    check(changed != *sample, "packet changes remain observable");
    check(changed.SameControls(*sample), "packet-only churn does not exhaust logging");
    changed.buttons ^= 1;
    check(!changed.SameControls(*sample), "single button edge remains observable");
    InputPass pass;
    check(!pass.MatchesEnd(20100, 100), "unpaired end rejected");
    pass.Begin(0x100000, 100, 7);
    check(pass.MatchesEnd(0x100000 + 20100, 101), "reviewed manager relationship");
    check(!pass.MatchesEnd(0x100000 + 20096, 101), "wrong manager rejected");
    check(!pass.MatchesEnd(0x100000 + 20100, 99), "regressing clock rejected");
    pass.changed = true; pass.polls = 3;
    pass.Begin(0xfffffff0, 200, 8);
    check(!pass.MatchesEnd(std::uint32_t(0xfffffff0ull + 20100), 201),
          "manager arithmetic cannot wrap");
    check(!pass.changed && !pass.polls && pass.began_frame == 8,
          "a new pass cannot retain earlier events");
    check(CoreInputBlock(1) == 0x888752F0 && CoreInputBlock(2) == 0x8887D818 && !CoreInputBlock(3),
          "overlapping modules retain separate reviewed input blocks");
    std::array<std::uint8_t, 176> core{};
    core[3] = 0x04; core[15] = 0x18; core[163] = 0x02; core[175] = 0x40;
    const auto decoded = DecodeCoreInput(core);
    check(decoded && decoded->previous == std::array<std::uint32_t,2>{4,2} &&
          decoded->current == std::array<std::uint32_t,2>{0x18,0x40}, "exact per-player current and prior masks");
    check(!DecodeCoreInput(std::span(core).first(175)), "truncated second player is rejected");
    const auto age = SinceCoreBuild(100, 7, 16766, 8);
    check(age && age->us == 16666 && age->frames == 1, "build-to-copy age retains guest epoch");
    check(!SinceCoreBuild(0,7,200,8) && !SinceCoreBuild(100,7,99,8) &&
          !SinceCoreBuild(100,7,200,6), "missing or regressing build cannot yield an age");
    check(RenderAddresses(1)[1] == 0x886F4CD0 && RenderAddresses(2)[1] == 0x886FD1F8 &&
          RenderAddresses(3) == std::array<std::uint32_t,5>{}, "disc render layouts do not borrow TU globals");
    const std::array<std::uint8_t, 4> word{0x88,0x67,0x12,0x34};
    check(RenderWord(word) == 0x88671234 && !RenderWord(std::span(word).first(3)),
          "render words require complete big-endian input");
    RenderWait wait{true, 2, 7, 100, 5};
    check(wait.Matches(7, 33432, 7), "guest frame wait retains generation and epochs");
    check(!wait.Matches(8, 33432, 7) && !wait.Matches(7, 99, 7) && !wait.Matches(7, 33432, 4),
          "reload and backwards clocks do not pair an old wait");
    wait.depth = 0;
    check(!wait.Matches(7, 33432, 7), "invalid wait depth rejected");
    wait.depth = 17;
    check(!wait.Matches(7, 33432, 7), "unbounded wait depth rejected");
    wait.depth = 1; wait.active = false;
    check(!wait.Matches(7, 33432, 7), "missing begin cannot pair a wait end");
    std::cout << passed << " SDOJ input/render observation checks passed\n";
}
