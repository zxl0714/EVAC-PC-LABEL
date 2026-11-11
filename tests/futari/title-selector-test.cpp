#include "futari_init.h"
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
uint32_t selected{}, calls{};
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

// Replace only the host device assignment. Execute the generated guest
// selector unchanged, including the configured hook and both index copies.
void FutariSelectLocalController(PPCRegister& user) {
    selected = user.u32;
    ++calls;
    user.u64 = 0;
}

int main() {
    try {
        alignas(32) std::array<uint8_t, 2048> memory{};
        uint8_t* base = memory.data();
        constexpr uint32_t manager = 256;
        for (uint32_t slot = 0; slot <= 4; ++slot) {
            memory.fill(0);
            REX_STORE_U32(manager + 36, 0xFFFFFFFF);
            if (slot < 4) REX_STORE_U32(manager + 68 + 132 * slot, 1);
            PPCContext context{};
            context.r3.u64 = manager;
            selected = 0xFFFFFFFF;
            calls = 0;
            sub_82190A90(context, base);
            if (slot < 4) {
                Check(calls == 1 && selected == slot, "Start did not claim the pressed controller");
                Check(context.r3.u32 == 0, "Profile selection retained the physical slot");
                Check(REX_LOAD_U32(manager + 36) == 0, "Menu input retained the physical slot");
            } else {
                Check(!calls && context.r3.u32 == 0xFFFFFFFF, "Idle title selected a controller");
                Check(REX_LOAD_U32(manager + 36) == 0xFFFFFFFF, "Idle input manager changed");
            }
        }
        std::puts("Generated Futari title selection passed: four slots and idle");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
