#include "core_phase.h"
#include <rex/ppc.h>
#include <rex/logging.h>
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {
thread_local guwange::performance::CorePhase phase;
thread_local std::uint32_t context;
thread_local std::uint64_t frames;
bool Enabled() {
    using Policy=bool (*)();
    static const auto policy=reinterpret_cast<Policy>(
        GetProcAddress(GetModuleHandleW(nullptr),"GuwangeCoreLowLatency"));
    return policy && policy();
}
}

void GuwangeCoreBegin(PPCContext&,std::uint8_t* base) {
    const bool enabled=Enabled();
    phase.Begin(enabled);
    context=0;
    if (enabled) {
        std::memcpy(&context,base+GUWANGE_CORE_CONTEXT,sizeof(context));
        context=__builtin_bswap32(context);
    }
}

// Entry to the original input/replay callback trampoline, before its PC and
// cycle counter advance. The unrolled dispatcher may revisit this unchanged
// entry until its next yield check; none of those visits consume input.
bool GuwangeCoreInput(PPCContext&,std::uint8_t* base) {
    if (!phase.DeferInput()) return false;
    base[context+1630]=1;
    return true;
}

// After the original end-of-frame store. Continue only to the next input
// callback, keeping all original logic, sprite writes and bank selection.
void GuwangeCoreYield(PPCContext&,std::uint8_t* base) {
    if (phase.ContinueAtYield()) base[context+1630]=0;
}

void GuwangeCoreEnd(PPCContext&,std::uint8_t*) {
    if (phase.StoppedAtInput() && ++frames%600==1)
        REXLOG_INFO("GUWANGE_CORE_PHASE module={} frames={} boundary=before_input_callback",GUWANGE_CORE_NAME,frames);
}
