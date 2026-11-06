#pragma once
#include <chrono>
#include <cstdio>

namespace cave::ps2 {
// Startup-only milestones. No periodic output in the normal rendering loop.
inline void StartupTrace(const char* phase) {
    using Clock=std::chrono::steady_clock;
    static const auto origin=Clock::now();
    const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-origin).count();
    std::fprintf(stderr,"[ps2-startup] elapsed_ms=%.3f phase=%s\n",ms,phase);
}
}
