#include <rex/ppc.h>

// These one-time initialization observations never modify guest state.
void GuwangeBootApplication(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT application={:08X}",result.u32);
}
void GuwangeBootEngine(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT engine={:08X}",result.u32);
}
void GuwangeBootDeviceAllocation(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT graphics_allocation={:08X}",result.u32);
}
void GuwangeBootDeviceInit(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT graphics_init={:08X}",result.u32);
}
void GuwangeBootDeviceReset(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT graphics_reset={:08X}",result.u32);
}
void GuwangeBootDeviceResult(PPCRegister& result) {
    REXLOG_INFO("GUWANGE_BOOT graphics_hresult={:08X}",result.u32);
}
void GuwangeBootSubsystems(PPCRegister& result, PPCRegister& engine_base) {
    REXLOG_INFO("GUWANGE_BOOT subsystems={:08X} engine_base={:08X}", result.u32, engine_base.u32);
}
void GuwangeBootThreadObject(PPCRegister& result, PPCRegister& runnable) {
    REXLOG_INFO("GUWANGE_BOOT thread_object={:08X} runnable={:08X}", result.u32, runnable.u32);
}
void GuwangeBootThreadDispatch(PPCRegister& target, PPCRegister& object,
                              PPCRegister& runnable, PPCRegister& engine) {
    REXLOG_INFO("GUWANGE_BOOT dispatch={:08X} object={:08X} runnable={:08X} engine={:08X}",
                target.u32, object.u32, runnable.u32, engine.u32);
}
void GuwangeBootThreadStarted(PPCRegister& result, PPCRegister& runnable) {
    REXLOG_INFO("GUWANGE_BOOT thread_started={:08X} runnable={:08X}", result.u32, runnable.u32);
}
void GuwangeBootMainExit(PPCRegister& flag, PPCRegister& runnable) {
    REXLOG_INFO("GUWANGE_BOOT main_exit last_r11={:08X} runnable={:08X}", flag.u32, runnable.u32);
}
void GuwangeBootCreateThread(PPCRegister& result, PPCRegister& object) {
    REXLOG_INFO("GUWANGE_BOOT create_thread_handle={:08X} object={:08X}", result.u32, object.u32);
}
