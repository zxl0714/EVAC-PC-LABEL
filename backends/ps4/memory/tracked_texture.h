#pragma once
#include "write_tracker.h"
#include "../graphics/texture.h"
namespace kt::gfx { class VulkanDevice; }

namespace kt::memory {
struct TrackedTextureStats {
    std::uint64_t generation_hits{}, content_hits{}, content_misses{}, compared_bytes{}, snapshot_bytes{};
};
// Native memory boundary, not a graphics-library memory reader. Each Decode
// pins the current guest allocation before inspecting any retained proof.
// Witnesses hold weak pixels only; the existing content/GPU caches own budgets.
class TrackedTextureCache {
public:
    explicit TrackedTextureCache(bool tracking = true, std::uint64_t maximum_bytes = 512 * 1024 * 1024,
                                 std::size_t maximum_entries = 16)
        : content_(maximum_bytes, maximum_entries), maximum_witnesses_(maximum_entries), tracking_(tracking) {}
    std::shared_ptr<const std::vector<std::uint8_t>> Decode(const kt::gfx::TextureLayout& layout);
    TrackedTextureStats Stats() const { return stats_; }
    std::uint64_t Bytes() const { return content_.Bytes(); }
    std::size_t Witnesses() const { return witnesses_.size(); }
private:
    struct Witness {
        kt::gfx::TextureLayout layout;
        WriteStamp proof;
        std::weak_ptr<const std::vector<std::uint8_t>> pixels;
    };
    kt::gfx::TextureDecodeCache content_;
    std::list<Witness> witnesses_;
    std::size_t maximum_witnesses_{};
    bool tracking_{};
    TrackedTextureStats stats_;
};
bool TrackedTextureSelfTest();
bool TrackedTextureGpuSelfTest(kt::gfx::VulkanDevice& device);
}
