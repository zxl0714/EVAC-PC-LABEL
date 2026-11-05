#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cave::ps4 {
enum class SnapshotOwnership { Free, Writing, Ready, Presenting };

// Both callers hold the ownership mutex. The copy producer and the output
// reader can finish in either order after early publication.
template<class Slot>
void FinishSnapshotCopy(Slot& slot, bool accepted) {
    slot.copy_pending = false;
    if (slot.reader_done || !accepted) slot.state = SnapshotOwnership::Free;
}
template<class Slot>
void FinishSnapshotRead(Slot& slot) {
    slot.reader_done = true;
    if (!slot.copy_pending) slot.state = SnapshotOwnership::Free;
}

// Caller holds the ownership mutex. A published snapshot may replace the
// active image before command recording. GPU readers wait for its copy tick.
template<class Slots>
bool LatchNewestSnapshot(Slots& slots, std::optional<std::uint32_t>& ready, std::uint32_t& active) {
    if(!ready)return false;
    const auto newest=*ready;
    if(active>=slots.size() || newest>=slots.size() || active==newest ||
       slots[active].state!=SnapshotOwnership::Presenting || slots[newest].state!=SnapshotOwnership::Ready)
        throw std::runtime_error("Invalid presentation snapshot ownership");
    slots[active].state=SnapshotOwnership::Free;
    slots[newest].state=SnapshotOwnership::Presenting;
    active=newest;ready.reset();
    return true;
}

// Complete publishes the image, then waits for the guest copy before pacing
// the guest flip. The output queue independently waits on the copy timeline.
template<class Publish,class Pace>
bool PublishThenPace(Publish&& publish,Pace&& pace) {
    std::forward<Publish>(publish)();
    return std::forward<Pace>(pace)();
}
}
