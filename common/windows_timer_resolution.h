#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>

namespace cave {
// Timed Windows condition-variable waits share the process timer resolution.
// Keep requests balanced through failed initialization and explicit shutdown.
class ScopedTimerResolution {
public:
    ScopedTimerResolution()=default;
    ~ScopedTimerResolution(){Reset();}
    ScopedTimerResolution(const ScopedTimerResolution&)=delete;
    ScopedTimerResolution& operator=(const ScopedTimerResolution&)=delete;
    bool Enable(){if(!active_)active_=timeBeginPeriod(1)==TIMERR_NOERROR;return active_;}
    void Reset(){if(active_){timeEndPeriod(1);active_=false;}}
    bool Active() const{return active_;}
private:
    bool active_=false;
};
}
