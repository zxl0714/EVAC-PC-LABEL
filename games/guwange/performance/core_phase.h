#pragma once

namespace guwange::performance {
// One native input callback and one game update per host frame. The original
// callback handles replay input too; never replace it with a direct pad read.
class CorePhase {
public:
    void Begin(bool enabled) { enabled_=enabled; input_=false; stopped_=false; yields_=0; }
    bool DeferInput() {
        if (!enabled_) return false;
        if (!input_) { input_=true; return false; }
        stopped_=true;
        return true;
    }
    bool ContinueAtYield() {
        // Boot paths without input retain their original boundary. A second
        // native yield always stops, even if the expected callback is absent.
        return enabled_ && input_ && !stopped_ && ++yields_==1;
    }
    bool StoppedAtInput() const { return stopped_; }
private:
    bool enabled_{},input_{},stopped_{};
    unsigned yields_{};
};
}
