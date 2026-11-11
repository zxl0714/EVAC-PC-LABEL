#include "qos_startup.h"
#include "futari_init.h"
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

void FutariQosCreateSuspended(PPCRegister& flags) {
    flags.u32 = futari::compat::SuspendQosCreate(flags.u32);
}

void FutariQosResumeAfterPublish(PPCContext& context, uint8_t* base) {
    const auto handle = futari::compat::PublishedQosThread(context.r31.u32,
        [base](uint32_t address) { return REX_LOAD_U32(address); });
    // A failed native CreateThread returned zero and has nothing to resume.
    if (!handle) return;
    auto thread = rex::Runtime::instance()->kernel_state()->object_table()
        ->LookupObject<rex::system::XThread>(handle);
    if (!thread) REX_FATAL("Futari QoS startup: missing thread {:08X}", handle);
    uint32_t suspended = 0;
    const auto status = thread->Resume(&suspended);
    if (status != 0)
        REX_FATAL("Futari QoS startup: resume failed {:08X}", status);
    REXLOG_INFO("Futari QoS startup: system published; resumed thread={:08X} suspend_count={}",
        handle, suspended);
}
