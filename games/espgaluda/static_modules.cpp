#include "ps2_runtime.h"

extern const uint32_t g_ps2ArcadeFunctionTableBase;
extern const uint32_t g_ps2ArcadeFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2ArcadeFunctionTable[];
extern const uint32_t g_ps2ArrangeFunctionTableBase;
extern const uint32_t g_ps2ArrangeFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2ArrangeFunctionTable[];

void RegisterEspgaludaStaticModules(PS2Runtime& runtime) {
  // SLPS-25352 A0A0/A0A8 and A0A9, verified MWo3 headers. Both originals
  // load at 0x730800; the original loader's header selects the native bank.
  runtime.registerStaticModule({16,0x730800,0x4c3ac0,0x10e000,0x2e100,
    g_ps2ArcadeFunctionTableBase,{g_ps2ArcadeFunctionTable,g_ps2ArcadeFunctionTableSlotCount}});
  runtime.registerStaticModule({17,0x730800,0x402840,0xf3700,0x21700,
    g_ps2ArrangeFunctionTableBase,{g_ps2ArrangeFunctionTable,g_ps2ArrangeFunctionTableSlotCount}});
}
