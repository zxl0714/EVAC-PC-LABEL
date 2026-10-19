#include "tracked_texture.h"
#include "../native_memory.h"
#include <stdexcept>

namespace kt::memory {
namespace {
struct ReadPin {
    void* address{};
    explicit ReadPin(const kt::gfx::TextureLayout& layout) {
        std::uint64_t physical{};
        if (!RetainNativeMemoryRange(reinterpret_cast<void*>(layout.address), layout.bytes, &physical, 1)) {
            throw std::runtime_error("Texture is not live CPU-readable direct memory");
        }
        address = reinterpret_cast<void*>(layout.address);
    }
    ~ReadPin() { ReleaseNativeMemoryRange(address, 1); }
};
}
std::shared_ptr<const std::vector<std::uint8_t>> TrackedTextureCache::Decode(const kt::gfx::TextureLayout& layout) {
    const ReadPin pin(layout);
    if (tracking_) {
        for (auto it = witnesses_.begin(); it != witnesses_.end(); ++it) {
            if (it->layout != layout) { continue; }
            if (it->proof.Unchanged()) {
                if (auto pixels = it->pixels.lock()) {
                    ++stats_.generation_hits;
                    witnesses_.splice(witnesses_.begin(), witnesses_, it);
                    return pixels;
                }
            }
            witnesses_.erase(it); break;
        }
    }
    // Acquire BEFORE content comparison/copy. An overlapping host write may
    // make Track return empty; fallback still captures owned current bytes but
    // must not publish a new proof after that write happened to finish.
    const auto before = tracking_ ? TrackNativeMemoryRange(pin.address, layout.bytes) : WriteStamp{};
    const auto hits = content_.hits, misses = content_.misses;
    const auto pixels = content_.Decode(layout, {static_cast<const std::uint8_t*>(pin.address), static_cast<std::size_t>(layout.bytes)});
    stats_.compared_bytes += layout.bytes;
    stats_.content_hits += content_.hits - hits; stats_.content_misses += content_.misses - misses;
    if (content_.misses != misses) { stats_.snapshot_bytes += layout.bytes; }
    if (maximum_witnesses_ && before.Unchanged()) {
        while (witnesses_.size() >= maximum_witnesses_) { witnesses_.pop_back(); }
        witnesses_.push_front({layout, before, pixels});
    }
    return pixels;
}
}
