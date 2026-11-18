#pragma once

#include "common/death_label_host.h"
#include "common/launch.h"
#include <span>

struct _CONTEXT;

namespace cave::ps4 {
class DaioujouDeathLabel final {
public:
    DaioujouDeathLabel() = default;
    ~DaioujouDeathLabel();
    DaioujouDeathLabel(const DaioujouDeathLabel&) = delete;
    DaioujouDeathLabel& operator=(const DaioujouDeathLabel&) = delete;

    bool Prepare(const fs::path& root, const Ini& ini, std::string_view profile);
    void SetGadgetSprites(fs::path path) { gadget_sprites_ = std::move(path); }
    DeathLabelResult Run(const DeathLabelActions& actions, const DeathLabelScreen& screen,
                         const DeathLabelAudio& audio);
    static void WriteGraphicsCrashTrace(void* file, const _CONTEXT& fault) noexcept;
    static void InstallBinding(std::span<std::uint8_t> image, std::span<const std::uint8_t,32> identity);

private:
    fs::path prepared_, disc_, profile_, gadget_sprites_;
    void* library_{};
    void* thunk_{};
    void* session_{};
    RunDeathLabel entry_{};
    ReleaseDeathLabel release_{};
    WriteDeathLabelGraphicsTrace graphics_trace_{};
    float volume_{};
};
}
