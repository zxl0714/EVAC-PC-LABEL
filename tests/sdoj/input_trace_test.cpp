#include "diagnostics/input_trace_policy.h"
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
    std::cout << passed << " SDOJ input observation checks passed\n";
}
