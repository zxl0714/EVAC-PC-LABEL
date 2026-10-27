#pragma once
#include <cstdint>

namespace espgaluda2::performance {
template <typename Memory>
std::uint32_t ReadGuestWord(const Memory& memory, std::uint32_t address) {
    // E0000000 aliases have a host mapping offset. Match generated guest loads.
    return __builtin_bswap32(*memory.template TranslateVirtual<const volatile std::uint32_t*>(address));
}
// The original unsigned subtraction/comparison, including 32-bit wraparound.
inline bool FencePending(std::uint32_t last_submitted, std::uint32_t target,
                         std::uint32_t completed) {
    return std::uint32_t(last_submitted - target) < std::uint32_t(last_submitted - completed);
}
void StartFenceWait();
void StopFenceWait();
}
