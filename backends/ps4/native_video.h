#pragma once
#include <cstdint>
#include <string_view>
#include <memory>
#include <atomic>
#include <functional>
#include <span>
#include <string>
#include "graphics/display_layout.h"
#include "diagnostics/present_timing.h"
namespace kt::gfx { class VulkanDevice; }
class NativeResidentGpu;

struct NativeVideoGpuContext {
    kt::gfx::VulkanDevice* device;
    const kt::gfx::DisplayLayout& layout;
    std::span<std::uint8_t> backing;
    std::uint64_t address;
    std::span<std::atomic<std::uint64_t>> labels;
    NativeResidentGpu* resident{};
    NativeVideoGpuContext(kt::gfx::VulkanDevice& gpu, const kt::gfx::DisplayLayout& display,
        std::span<std::uint8_t> bytes, std::uint64_t base, std::span<std::atomic<std::uint64_t>> live_labels)
        : device(&gpu), layout(display), backing(bytes), address(base), labels(live_labels) {}
    NativeVideoGpuContext(kt::gfx::VulkanDevice* gpu, const kt::gfx::DisplayLayout& display,
        std::span<std::uint8_t> bytes, std::uint64_t base, std::span<std::atomic<std::uint64_t>> live_labels, NativeResidentGpu* runtime)
        : device(gpu), layout(display), backing(bytes), address(base), labels(live_labels), resident(runtime) {}
};
// Holds the port lock and registered-memory pins across synchronous GPU work.
// Closing/unregistering the output cannot invalidate the callback's resources.
// Cancellation is not a completed frame and is not an invalid port/backend.
enum class NativeVideoResult { Completed, CloseRequested, Unavailable };
// Completed host wall-time scopes, not GPU timestamps or physical scanout.
// Returned to the caller so normal (<20 ms) frames need no extra log record.
struct NativeVideoPresentTiming {
    bool completed{};
    std::uint64_t duration_us{}, lock_wait_us{}, prepare_us{}, display_clock_wait_us{}, backend_present_us{}, completion_us{};
    kt::diagnostics::BackendPresentTiming backend;
};
NativeVideoResult WithNativeVideoGpu(int handle, unsigned buffer, const std::function<void(NativeVideoGpuContext&)>& execute);
NativeVideoResult PresentNativeVideoGpu(int handle, unsigned buffer, unsigned mode, std::int64_t argument,
    NativeVideoPresentTiming* timing = nullptr);
// Retire presentation after the GNM submission scope has returned. This is an
// idempotent process-closing barrier, not guest-thread or GPU-context teardown.
bool CompleteNativePresentationShutdown(int handle);

struct NativeVideoLabelReference { std::uint64_t address{}; std::shared_ptr<void> owner; };
// The lease only pins memory during command construction. Submission must
// validate the live port again before executing any label wait/write.
NativeVideoLabelReference AcquireNativeVideoBufferLabel(int handle, unsigned index);

std::uint64_t ResolveVideoImport(std::string_view identifier, std::string_view library, std::string_view module);
bool VideoSelfTest();
struct NativeDisplaySettings {
    std::wstring title = L"Ketsui PC - development preview | Alt+Enter / F11: fullscreen";
    bool fullscreen{}, vsync{true};
    unsigned window_width{1280}, window_height{720};
};
void ConfigureNativeDisplay(NativeDisplaySettings settings);
NativeDisplaySettings GetNativeDisplaySettings();
// Offline benchmark only: skip the software flip opportunity, never GPU fences
// or the guest's vblank clock/events. Host vsync must already be disabled.
void ConfigureNativeUnpacedPresentation(bool enabled);
bool GraphicsSelfTest(bool interactive = false);
int VideoDemo();
