#include "main_wait.h"

#include <chrono>
#include <thread>
#include <rex/ppc.h>
#include <rex/system/xthread.h>

void GuwangeMainThreadWait(PPCRegister& sampled_flag) {
    REXLOG_INFO("GUWANGE_BOOT main_keepalive sampled_flag={:08X}", sampled_flag.u32);
    cave::guwange::KeepMainThreadAlive(sampled_flag.u32,
        [] { rex::system::XThread::CheckTitleTermination(); },
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
}
