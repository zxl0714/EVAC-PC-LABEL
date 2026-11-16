#include "game_overrides.h"
#include "backends/ps2/sdk_binding.h"
#include <stdexcept>

namespace {
template<uint32_t Address>
void BindDiscSearch(PS2Runtime& runtime) {
    if (!runtime.replaceFunction(Address, &cave::ps2::SdkEntry<&cave::ps2::SearchSingleLayer>))
        throw std::runtime_error("Cannot bind DaiOuJou single-layer disc search");
}
}

PS2_REGISTER_GAME_OVERRIDE("DaiOuJou SLPS-25233 disc search", "", 0x00100008u, 0x912d63ceu, BindDiscSearch<0x0010dac8u>)
PS2_REGISTER_GAME_OVERRIDE("DaiOuJou Death Label disc search", "", 0x00100008u, 0x1bef5c3au, BindDiscSearch<0x0010dac8u>)
PS2_REGISTER_GAME_OVERRIDE("DaiOuJou Gallery disc search", "", 0x00100008u, 0x6e517dc7u, BindDiscSearch<0x0010d9c8u>)
