#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cave::ps4 {
enum class SnapshotOwnership { Free, Writing, Ready, Presenting };

// Caller holds the ownership mutex. Only a complete, published snapshot may
// replace the active image, before any commands reference its pixels.
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

// Publishing waits for the guest->owned-image copy. Guest flip completion is
// still paced afterwards: no extra game update, fake vblank or removed fence.
template<class Publish,class Pace>
bool PublishThenPace(Publish&& publish,Pace&& pace) {
    std::forward<Publish>(publish)();
    return std::forward<Pace>(pace)();
}
}
