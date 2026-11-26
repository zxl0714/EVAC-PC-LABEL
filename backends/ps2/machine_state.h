#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

class PS2Runtime;
namespace cave::ps2 {
class GsDevice;

// Run on the parked EE dispatcher. The visitor synchronously drains all
// earlier GS commands and executes the supplied function on the Vulkan owner.
struct MachineState {
    using GraphicsOwner = std::function<void(const std::function<void(GsDevice&)>&)>;
    static std::vector<uint8_t> Save(PS2Runtime& runtime, const GraphicsOwner& graphics);
    static void Load(PS2Runtime& runtime, const GraphicsOwner& graphics, std::span<const uint8_t> bytes);
    static void PauseAudio(PS2Runtime& runtime, bool paused, bool clearQueued = false);
};
}
