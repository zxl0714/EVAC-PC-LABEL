#include "futari_init.h"
#include <rex/input/input_system.h>
#include <rex/runtime.h>

void FutariSelectLocalController(PPCRegister& user) {
    if (user.u32 >= 4) return; // Preserve the original no-user path.
    auto* input = dynamic_cast<rex::input::InputSystem*>(rex::Runtime::instance()->input_system());
    if (!input || !input->SelectPrimaryController(user.u32)) return;
    REXLOG_INFO("Futari controller selection: guest slot={} local profile=0", user.u32);
    // The original instructions store this index into the input manager and
    // return it to the title/profile flow. Both must use the same local slot.
    // Later GetState(0) reads the selected pad, plus the usual keyboard input.
    user.u64 = 0;
}
