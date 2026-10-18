#pragma once
#include <rex/input/input.h>

namespace rex::input {
// Only physical pads are remapped; keyboard binds already name Xbox outputs.
void RemapPhysicalGamepad(X_INPUT_GAMEPAD& gamepad);
}
