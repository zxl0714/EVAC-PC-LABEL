#pragma once
#include "gs_scanout.h"
#include <optional>

namespace cave::ps2 {
enum class ReadyResult { Confirmed, Held, Correct };

// A delayed flip may repeat the previous front page. Ibara's render worker
// has already published the next page, so keep it on screen in that case.
inline ReadyResult ConfirmReadyFrame(GsFrame ready,const GsFrame& actual,
                                     std::optional<uint64_t> previousPage,
                                     bool contentUnchanged,bool completedBackPage) {
    if(!contentUnchanged)return ReadyResult::Correct;
    ready.presentAfter={};
    if(ready==actual)return ReadyResult::Confirmed;
    if(completedBackPage && previousPage==actual.dispfb2) {
        ready.dispfb2=actual.dispfb2;
        if(ready==actual)return ReadyResult::Held;
    }
    return ReadyResult::Correct;
}
}
