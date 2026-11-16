#pragma once

#include <cstdint>

struct R5900Context;

namespace cave::daioujou_ps2 {

enum class DeathLabelAttack { Shot, Laser, Aura };
void ObserveDeathLabelDamage(const std::uint8_t* memory, const R5900Context* context,
                             DeathLabelAttack attack, std::uint32_t operands);

}
