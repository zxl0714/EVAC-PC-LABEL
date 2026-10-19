#include "native_video.h"
#include "native_async_log.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_user.h"
#include "native_profile.h"
#include "native_memory.h"
#include "native_display_clock.h"
#include "native_equeue.h"
#include "native_pad.h"
#include "graphics/display_layout.h"
#include "graphics/vulkan_device.h"
#include "graphics/shader_compiler.h"
#include "native_gpu.h"
#include "native_gnm.h"
#include "native_resident_gpu.h"
#include "diagnostics/monotonic_clock.h"

#include <windows.h>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int kValue = static_cast<int>(0x80290001u);
constexpr int kAddress = static_cast<int>(0x80290002u);
constexpr int kBusy = static_cast<int>(0x80290009u);
constexpr int kHandle = static_cast<int>(0x8029000bu);
constexpr int kMemory = static_cast<int>(0x8029100cu);
constexpr int kFatal = static_cast<int>(0x802900ffu);
constexpr wchar_t kClass[] = L"KetsuiNativeVideo";
constexpr UINT kFinish = WM_APP + 210;
constexpr LONG kStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr unsigned kWidth = kNativeProfile.output_width, kHeight = kNativeProfile.output_height;

bool IsTimelineMarkerKey(UINT message, WPARAM key, LPARAM flags) noexcept {
    return message == WM_KEYDOWN && key == VK_F8 && !(flags & (1ll << 30));
}

void RecordTimelineMarker() {
    static std::atomic<std::uint64_t> next_id{1};
    const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
    const auto clock = kt::diagnostics::MonotonicMicroseconds();
    std::printf("{\"event\":\"native_timeline_marker\",\"id\":%llu,\"clock_us\":%llu,\"source\":\"F8\"}\n", id, clock);
}

// A virtual SDR output mode for the guest, independent of resized PC client
// area. Refresh=UNKNOWN until a real presentation clock is connected.
struct Resolution {
    std::int32_t full_width, full_height, pane_width, pane_height;
    std::uint64_t refresh;
    float diagonal_inches;
    std::uint16_t flags, reserved0;
    std::uint32_t reserved1[3], tail_padding;
};
static_assert(sizeof(Resolution) == 48 && offsetof(Resolution, refresh) == 16);

using BufferAttribute = kt::gfx::DisplayAttribute;

struct MemoryPin {
    void* address{};
    std::uint64_t physical{};
    MemoryPin() = default;
    MemoryPin(const MemoryPin&) = delete;
    MemoryPin& operator=(const MemoryPin&) = delete;
    MemoryPin(MemoryPin&& other) noexcept : address(std::exchange(other.address, nullptr)), physical(other.physical) {}
    MemoryPin& operator=(MemoryPin&& other) noexcept {
        if (address) { ReleaseNativeMemoryRange(address, 0x33); }
        address = std::exchange(other.address, nullptr); physical = other.physical;
        return *this;
    }
    ~MemoryPin() { if (address) { ReleaseNativeMemoryRange(address, 0x33); } }
    bool Retain(void* pointer, std::uint64_t bytes) {
        if (!RetainNativeMemoryRange(pointer, bytes, &physical, 0x33)) { return false; }
        address = pointer; return true;
    }
};
struct DisplaySurface {
    kt::gfx::VulkanDevice* device;
    kt::gfx::DisplayLayout layout;
    std::uint64_t id;
    DisplaySurface(kt::gfx::VulkanDevice* owner, const kt::gfx::DisplayLayout& description)
        : device(owner), layout(description), id(owner ? owner->CreateDisplayImage(description) : 0) {}
    ~DisplaySurface() {
        try { if (device) { device->DestroyDisplayImage(id); } }
        catch (...) { UnsupportedApi("native_video_image_release_failure", id, nullptr); }
    }
};
struct DisplayBuffer { MemoryPin memory; std::shared_ptr<DisplaySurface> surface; };
struct DisplayGroup {
    int first{};
    std::vector<DisplayBuffer> buffers;
};

struct WindowJob {
    NativeDisplaySettings display;
    std::array<std::atomic<std::uint64_t>, 16> buffer_labels{};
    std::shared_ptr<NativeDisplayClock> clock;
    HANDLE ready{}, quit{}, thread{};
    std::atomic<HWND> window{};
    std::atomic<unsigned> width{}, height{};
    std::atomic<bool> fullscreen{}, quit_requested{}, renderer_active{};
    std::atomic<bool> zero_extent_waiting{};
    std::atomic<bool> startup_visible{true};
    std::atomic<unsigned> toggles{};
    std::atomic<DWORD> failure{};
    bool visible = true;
    WINDOWPLACEMENT saved{}; // UI-thread-only
    LONG_PTR saved_style{};
    ~WindowJob() {
        if (ready) { CloseHandle(ready); }
        if (quit) { CloseHandle(quit); }
        if (thread) { CloseHandle(thread); }
    }
};
struct VideoState {
    NativeDisplaySettings display;
    std::mutex mutex;
    std::shared_ptr<WindowJob> active;
    // Retain only the last port's label metadata, not its HWND, clock or OS
    // handles. Already-built commands may still carry those label addresses.
    std::shared_ptr<WindowJob> closed;
    int closed_handle{}, stopping_handle{};
    int handle{}, next = 1;
    bool startup_hidden{};
    bool diagnostic_unpaced{};
    // Groups (and their memory pins) are destroyed before their Vulkan owner.
    std::unique_ptr<kt::gfx::VulkanDevice> graphics;
    std::map<std::uint64_t, std::weak_ptr<DisplaySurface>> surface_cache;
    std::array<DisplayGroup, 4> groups;
};
VideoState& State() { static VideoState state; return state; }

bool ToggleFullscreen(HWND window, WindowJob& job) {
    if (!job.fullscreen.load()) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        job.saved.length = sizeof(job.saved);
        if (!GetWindowPlacement(window, &job.saved) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) { return false; }
        job.saved_style = GetWindowLongPtrW(window, GWL_STYLE);
        SetLastError(0);
        if (!SetWindowLongPtrW(window, GWL_STYLE, job.saved_style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) && GetLastError()) { return false; }
        const auto& bounds = monitor.rcMonitor;
        if (!SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                          SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
            SetWindowLongPtrW(window, GWL_STYLE, job.saved_style);
            SetWindowPlacement(window, &job.saved);
            return false;
        }
        job.fullscreen.store(true);
    } else {
        SetLastError(0);
        if (!SetWindowLongPtrW(window, GWL_STYLE, job.saved_style) && GetLastError()) { return false; }
        if (!SetWindowPlacement(window, &job.saved) || !SetWindowPos(window, nullptr, 0, 0, 0, 0,
             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER)) { return false; }
        job.fullscreen.store(false);
    }
    ++job.toggles;
    std::printf("{\"event\":\"native_video_fullscreen\",\"enabled\":%s,\"client_width\":%u,\"client_height\":%u}\n",
                job.fullscreen.load() ? "true" : "false", job.width.load(), job.height.load());
    return true;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* job = reinterpret_cast<WindowJob*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        job = static_cast<WindowJob*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(job));
        NativePadAttachWindow(window);
    }
    if (!job) { return DefWindowProcW(window, message, wparam, lparam); }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        NativePadWindowFocus(window, message == WM_SETFOCUS);
        if (message == WM_KILLFOCUS && GetCapture() == window) { ReleaseCapture(); }
    }
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) && NativePadMouseLeftMapped()) {
        if (message == WM_LBUTTONDOWN) {
            SetCapture(window);
            if (GetCapture() != window) { return 0; }
        }
        NativePadWindowKey(window, VK_LBUTTON, message == WM_LBUTTONDOWN);
        if (message == WM_LBUTTONUP && GetCapture() == window) { ReleaseCapture(); }
        return 0;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
        NativePadWindowKey(window, VK_LBUTTON, false);
        if (message == WM_CANCELMODE && GetCapture() == window) { ReleaseCapture(); }
    }
    if (message == WM_SIZE) {
        job->width.store(LOWORD(lparam)); job->height.store(HIWORD(lparam));
        std::printf("{\"event\":\"native_video_window_size\",\"width\":%u,\"height\":%u,\"minimized\":%s}\n",
            job->width.load(), job->height.load(), wparam == SIZE_MINIMIZED ? "true" : "false");
        return 0;
    }
    if (message == WM_DPICHANGED && !job->fullscreen.load()) {
        const auto* bounds = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if ((message == WM_KEYDOWN && wparam == VK_F11) ||
        (message == WM_SYSKEYDOWN && wparam == VK_RETURN && (lparam & (1ll << 29)))) {
        if (!(lparam & (1ll << 30)) && !ToggleFullscreen(window, *job)) {
            UnsupportedApi("native_video_fullscreen_failure", GetLastError(), nullptr);
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        NativePadWindowFocus(window, false);
        if (!job->quit_requested.exchange(true)) {
            std::puts("{\"event\":\"native_video_window_close_requested\",\"source\":\"WM_CLOSE\"}");
        }
        SetEvent(job->quit);
        // Keep HWND alive until the runtime has drained/detached the renderer.
        // Close() owns final destruction on this UI thread.
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    if (message == kFinish) { DestroyWindow(window); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        if (job->renderer_active.load()) { EndPaint(window, &paint); return 0; }
        FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (job->startup_visible.load()) {
            RECT bounds{}; GetClientRect(window, &bounds);
            const auto saved_dc = SaveDC(dc);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(180, 180, 180));
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(dc, L"Ketsui native runtime - graphics initializing", -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (saved_dc) { RestoreDC(dc, saved_dc); }
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_NCDESTROY) {
        NativePadDetachWindow(window);
        job->window.store(nullptr);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        // Observe only: a configured F8 game binding still receives the key.
        if (IsTimelineMarkerKey(message, wparam, lparam)) { RecordTimelineMarker(); }
        NativePadWindowKey(window, static_cast<unsigned>(wparam), message == WM_KEYDOWN);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

BOOL CALLBACK RegisterWindow(PINIT_ONCE, PVOID, PVOID*) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.style = CS_OWNDC;
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.lpszClassName = kClass;
    return RegisterClassExW(&type) != 0;
}

DWORD WINAPI WindowThread(void* argument) noexcept {
    std::unique_ptr<std::shared_ptr<WindowJob>> owned(static_cast<std::shared_ptr<WindowJob>*>(argument));
    const auto job = *owned;
    const auto previous_dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT bounds{0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight)};
    HWND window = nullptr;
    if (AdjustWindowRectExForDpi(&bounds, kStyle, FALSE, 0, GetDpiForSystem())) {
        window = CreateWindowExW(0, kClass, job->display.title.c_str(), kStyle,
             CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
             nullptr, nullptr, GetModuleHandleW(nullptr), job.get());
    }
    if (!window) { job->failure.store(GetLastError()); SetEvent(job->ready); }
    else {
        job->window.store(window);
        if (job->visible && job->display.fullscreen && !ToggleFullscreen(window, *job)) {
            job->failure.store(GetLastError() ? GetLastError() : ERROR_GEN_FAILURE);
            DestroyWindow(window);
            SetEvent(job->ready);
            if (previous_dpi) { SetThreadDpiAwarenessContext(previous_dpi); }
            return 0;
        }
        RECT client{};
        if (GetClientRect(window, &client)) {
            job->width.store(static_cast<unsigned>(client.right));
            job->height.store(static_cast<unsigned>(client.bottom));
        }
        if (job->visible) { ShowWindow(window, SW_SHOWNOACTIVATE); }
        SetEvent(job->ready);
        MSG message{};
        BOOL result{};
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (result == -1) {
            // Do not destroy an HWND still backing a live Vulkan surface when
            // its message pump fails. Report the host failure instead.
            UnsupportedApi("native_video_message_pump_failure", GetLastError(), nullptr);
        }
    }
    if (previous_dpi) { SetThreadDpiAwarenessContext(previous_dpi); }
    return 0;
}

int Open(int user, int bus, int index, const void* parameter, bool visible, const void* caller) {
    if (bus != 0 || index != 0 || (user != 0xff && (user != 0x1000 || !NativeUserInitialized()))) { return kValue; }
    if (parameter) { UnsupportedApi("sceVideoOutOpen_parameter", reinterpret_cast<std::uintptr_t>(parameter), caller); }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (state.active || state.stopping_handle || state.next == (std::numeric_limits<int>::max)()) { return kBusy; }
    static INIT_ONCE initialization = INIT_ONCE_STATIC_INIT;
    if (!InitOnceExecuteOnce(&initialization, RegisterWindow, nullptr, nullptr)) { return kFatal; }
    auto job = std::make_shared<WindowJob>();
    job->display = state.display;
    job->visible = visible;
    job->startup_visible.store(!state.startup_hidden);
    job->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    job->quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!job->ready || !job->quit) { return kMemory; }
    job->clock = std::make_shared<NativeDisplayClock>(static_cast<unsigned>(state.next),
        kNativeProfile.vblank_numerator, kNativeProfile.vblank_denominator);
    auto argument = std::make_unique<std::shared_ptr<WindowJob>>(job);
    state.active = job;
    job->thread = CreateThread(nullptr, 0, WindowThread, argument.get(), 0, nullptr);
    if (!job->thread) { state.active.reset(); return kMemory; }
    argument.release();
    const auto ready = WaitForSingleObject(job->ready, 5000);
    if (ready != WAIT_OBJECT_0) { UnsupportedApi("native_video_window_start_timeout", ready, caller); }
    if (!job->window.load()) {
        const auto stopped = WaitForSingleObject(job->thread, 5000);
        if (stopped != WAIT_OBJECT_0) { UnsupportedApi("native_video_window_start_failure", stopped, caller); }
        std::printf("{\"event\":\"native_video_host_error\",\"win32_error\":%lu}\n", job->failure.load());
        state.active.reset();
        return kFatal;
    }
    state.handle = state.next++;
    if (auto* resident = CurrentNativeResidentGpu()) { resident->SetWindow(job->window.load()); }
    std::printf("{\"event\":\"native_video_open\",\"handle\":%d,\"user\":%d,\"width\":%u,\"height\":%u,\"dpi\":%u,\"renderer\":false}\n",
                state.handle, user, job->width.load(), job->height.load(), GetDpiForWindow(job->window.load()));
    TraceNativeApi("sceVideoOutOpen", static_cast<unsigned>(state.handle), caller);
    return state.handle;
}

int CloseLocked(VideoState& state, int handle, const void* caller) {
    if (!state.active || handle != state.handle) { return kHandle; }
    const auto job = state.active;
    if (job->clock) { job->clock->Stop(); } // cancel waits/remove subscriptions before GPU and HWND teardown
    if (auto* resident = CurrentNativeResidentGpu()) { resident->SetWindow(nullptr); }
    if (state.graphics) {
        state.graphics->WaitIdle();
    }
    for (auto& group : state.groups) { group.buffers.clear(); }
    state.surface_cache.clear();
    state.graphics.reset(); // surface/device gone before HWND destruction
    const auto window = job->window.load();
    if (window && !PostMessageW(window, kFinish, 0, 0)) { return kFatal; }
    const auto stopped = WaitForSingleObject(job->thread, 5000);
    if (stopped != WAIT_OBJECT_0) { UnsupportedApi("native_video_window_close_timeout", stopped, caller); }
    if (job->window.load()) { return kFatal; }
    job->clock.reset();
    for (auto* owned : {&job->ready, &job->quit, &job->thread}) {
        if (*owned && !CloseHandle(*owned)) { return kFatal; }
        *owned = nullptr;
    }
    std::printf("{\"event\":\"native_video_close\",\"handle\":%d,\"quit_requested\":%s,\"fullscreen_toggles\":%u}\n",
                handle, job->quit_requested.load() ? "true" : "false", job->toggles.load());
    state.closed = job; state.closed_handle = handle;
    state.active.reset(); state.handle = 0;
    return 0;
}
int Close(int handle, const void* caller) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return CloseLocked(state, handle, caller);
}

int ResolutionStatus(int handle, Resolution* output) {
    if (!output) { return kAddress; }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (!state.active || handle != state.handle) { return kHandle; }
    *output = {kWidth, kHeight, kWidth, kHeight, 0, 0.0f, 0, 0, {0, 0, 0}, 0};
    return 0;
}

std::shared_ptr<NativeDisplayClock> PortClock(int handle) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (!state.active || state.handle != handle) { return nullptr; }
    return state.active->clock;
}

int LayoutError(kt::gfx::LayoutError error, const void* caller) {
    switch (error) {
    case kt::gfx::LayoutError::None: return 0;
    case kt::gfx::LayoutError::Value: return kValue;
    case kt::gfx::LayoutError::Format: return static_cast<int>(0x80290003u);
    case kt::gfx::LayoutError::Pitch: return static_cast<int>(0x80290004u);
    case kt::gfx::LayoutError::Resolution: return static_cast<int>(0x80290005u);
    case kt::gfx::LayoutError::Tiling: return static_cast<int>(0x80290007u);
    case kt::gfx::LayoutError::Aspect: return static_cast<int>(0x80290008u);
    case kt::gfx::LayoutError::Unsupported: UnsupportedApi("native_video_display_layout", 0, caller);
    }
    return kValue;
}

int RegisterBuffers(int handle, int first, void* const* addresses, int count, const BufferAttribute* attribute, const void* caller,
                    int test_failure_after = -1) {
    if (!addresses || !attribute) { return kAddress; }
    if (first < 0 || count <= 0 || count > 16 || first > 16 - count) { return kValue; }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (!state.active || handle != state.handle || state.stopping_handle) { return kHandle; }
    const auto described = kt::gfx::DescribeDisplay(*attribute, kNativeProfile.neo);
    if (const auto error = LayoutError(described.error, caller)) { return error; }
    const auto& layout = described.layout;
    int available = -1;
    for (unsigned index = 0; index < state.groups.size(); ++index) {
        const auto& group = state.groups[index];
        if (group.buffers.empty()) { if (available < 0) { available = static_cast<int>(index); } continue; }
        if (first < group.first + static_cast<int>(group.buffers.size()) && group.first < first + count) { return static_cast<int>(0x80290010u); }
    }
    if (available < 0) { return static_cast<int>(0x8029000fu); }
    std::erase_if(state.surface_cache, [](const auto& entry) { return entry.second.expired(); });
    DisplayGroup pending;
    pending.first = first;
    pending.buffers.reserve(static_cast<std::size_t>(count));
    // Validate/pin every padded range before creating a device or image. This
    // also preserves GPU permission bits through later Mprotect calls.
    for (int index = 0; index < count; ++index) {
        if (reinterpret_cast<std::uintptr_t>(addresses[index]) % layout.alignment) { return static_cast<int>(0x80290015u); }
        DisplayBuffer buffer;
        if (!buffer.memory.Retain(addresses[index], layout.bytes)) { return static_cast<int>(0x80290013u); }
        pending.buffers.push_back(std::move(buffer));
    }
    if (!state.graphics && !CurrentNativeResidentGpu()) { state.graphics = std::make_unique<kt::gfx::VulkanDevice>(state.active->window.load(), state.display.vsync); }
    int created{};
    for (auto& buffer : pending.buffers) {
        const auto physical = buffer.memory.physical;
        for (const auto& [existing, weak] : state.surface_cache) {
            const auto image = weak.lock();
            if (image && physical < existing + image->layout.bytes && existing < physical + layout.bytes &&
                (physical != existing || image->layout != layout)) {
                UnsupportedApi("native_video_surface_alias_layout", physical, caller);
            }
        }
        auto& cached = state.surface_cache[physical];
        buffer.surface = cached.lock();
        if (!buffer.surface) {
            // Deterministic rollback test, only passed by GraphicsSelfTest.
            // The game-facing SysV wrapper never supplies this argument.
            if (test_failure_after >= 0 && created == test_failure_after) { throw std::bad_alloc(); }
            buffer.surface = std::make_shared<DisplaySurface>(state.graphics.get(), layout);
            ++created;
            cached = buffer.surface;
        }
    }
    auto& published = state.groups[static_cast<std::size_t>(available)];
    published = std::move(pending);
    for (std::size_t index = 0; index < published.buffers.size(); ++index) {
        const auto& buffer = published.buffers[index];
        std::printf("{\"event\":\"native_video_register_buffer\",\"slot\":%d,\"group\":%d,\"address\":\"0x%llx\",\"physical\":\"0x%llx\",\"bytes\":%llu,\"image\":%llu}\n",
                    first + static_cast<int>(index), available, reinterpret_cast<std::uintptr_t>(buffer.memory.address), buffer.memory.physical, layout.bytes, buffer.surface->id);
    }
    TraceNativeApi("sceVideoOutRegisterBuffers", static_cast<unsigned>(count), caller);
    return available;
}

int UnregisterBuffers(int handle, int group_id) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (!state.active || state.handle != handle) { return kHandle; }
    if (group_id < 0 || group_id >= static_cast<int>(state.groups.size()) || state.groups[static_cast<std::size_t>(group_id)].buffers.empty()) { return kValue; }
    if (auto* resident = CurrentNativeResidentGpu()) { resident->Drain(); }
    else { state.graphics->WaitIdle(); }
    state.groups[static_cast<std::size_t>(group_id)].buffers.clear();
    std::erase_if(state.surface_cache, [](const auto& entry) { return entry.second.expired(); });
    return 0;
}

template <typename Function>
int Api(const void* caller, Function function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Native video error: %s\n", error.what());
        UnsupportedApi("native_video_host", 0, caller);
    }
}
int KT_SYSV VideoOpen(int user, int bus, int index, const void* parameter) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return Open(user, bus, index, parameter, true, caller); });
}
int KT_SYSV VideoClose(int handle) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return Close(handle, caller); });
}
int KT_SYSV VideoResolution(int handle, Resolution* output) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return ResolutionStatus(handle, output); });
}
void KT_SYSV SetBufferAttribute(BufferAttribute* output, std::uint32_t format, std::uint32_t tiling,
                               std::uint32_t aspect, std::uint32_t width, std::uint32_t height, std::uint32_t pitch) {
    if (!output) { UnsupportedApi("sceVideoOutSetBufferAttribute_null", 0, __builtin_return_address(0)); }
    // This helper initializes an ABI descriptor. Registration, layout and
    // presentation must validate/support that descriptor separately.
    *output = {format, tiling, aspect, width, height, pitch, 0, 0, 0};
    std::printf("{\"event\":\"native_video_buffer_attribute\",\"format\":\"0x%x\",\"tiling\":%u,\"aspect\":%u,\"width\":%u,\"height\":%u,\"pitch\":%u}\n",
                format, tiling, aspect, width, height, pitch);
    TraceNativeApi("sceVideoOutSetBufferAttribute", width, __builtin_return_address(0));
}
int KT_SYSV VideoRegister(int handle, int first, void* const* addresses, int count, const BufferAttribute* attribute) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return RegisterBuffers(handle, first, addresses, count, attribute, caller); });
}
int KT_SYSV VideoUnregister(int handle, int group) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return UnregisterBuffers(handle, group); });
}
int KT_SYSV SetFlipRate(int handle, int rate) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        const auto clock = PortClock(handle);
        if (!clock) { return kHandle; }
        if (rate < 0) { return kValue; }
        if (rate > 2) { UnsupportedApi("sceVideoOutSetFlipRate_rate", static_cast<unsigned>(rate), caller); }
        if (!clock->SetFlipRate(rate)) { return kHandle; }
        TraceNativeApi("sceVideoOutSetFlipRate", static_cast<unsigned>(rate), caller);
        return 0;
    });
}
int KT_SYSV WaitVblank(int handle) {
    const auto caller = __builtin_return_address(0);
    NativeHostOperationScope timing("sceVideoOutWaitVblank", "synchronization", static_cast<std::uint64_t>(handle), caller);
    return Api(caller, [&] { const auto clock = PortClock(handle); return clock && clock->WaitVblank() ? 0 : kHandle; });
}
int KT_SYSV VblankStatus(int handle, NativeVblankStatus* output) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        if (!output) { return kAddress; }
        const auto clock = PortClock(handle);
        if (!clock) { return kHandle; }
        *output = clock->Snapshot();
        return 0;
    });
}
int UpdateVideoEvent(std::uint64_t queue, int handle, void* user_data, bool flip, bool add) {
    const auto clock = PortClock(handle);
    if (!clock) { return kHandle; }
    return add ? clock->Subscribe(queue, flip, user_data) : clock->Unsubscribe(queue, flip);
}
int KT_SYSV AddFlipEvent(std::uint64_t queue, int handle, void* user_data) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        const auto result = UpdateVideoEvent(queue, handle, user_data, true, true);
        if (!result) { TraceNativeApi("sceVideoOutAddFlipEvent", queue, caller); }
        return result;
    });
}
int KT_SYSV AddVblankEvent(std::uint64_t queue, int handle, void* user_data) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] {
        const auto result = UpdateVideoEvent(queue, handle, user_data, false, true);
        if (!result) { TraceNativeApi("sceVideoOutAddVblankEvent", queue, caller); }
        return result;
    });
}
int KT_SYSV DeleteFlipEvent(std::uint64_t queue, int handle) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return UpdateVideoEvent(queue, handle, nullptr, true, false); });
}
int KT_SYSV DeleteVblankEvent(std::uint64_t queue, int handle) {
    const auto caller = __builtin_return_address(0);
    return Api(caller, [&] { return UpdateVideoEvent(queue, handle, nullptr, false, false); });
}
int KT_SYSV GetVideoEventId(const NativeKernelEvent* event) {
    if (!event) { return kAddress; }
    if (event->filter != -13) { return static_cast<int>(0x8029000du); }
    switch (event->ident) {
    case 6: return 0;
    case 7: case 0x63: return 1;
    case 0x59: return 2;
    case 0x51: return 8;
    case 0x58: return 12;
    default: return static_cast<int>(0x8029000du);
    }
}
int KT_SYSV GetVideoEventData(const NativeKernelEvent* event, std::int64_t* output) {
    if (!event || !output) { return kAddress; }
    if (event->filter != -13) { return static_cast<int>(0x8029000du); }
    auto value = event->data >> 16;
    if (event->ident == 6 && (value & 0x800000000000ull)) { value |= 0xffff000000000000ull; }
    *output = static_cast<std::int64_t>(value);
    return 0;
}
int KT_SYSV GetVideoEventCount(const NativeKernelEvent* event) {
    if (!event) { return kAddress; }
    if (event->filter != -13) { return static_cast<int>(0x8029000du); }
    return static_cast<int>((event->data >> 12) & 15);
}
int KT_SYSV HideStartup() {
    const auto caller = __builtin_return_address(0);
    auto& state = State(); std::lock_guard lock(state.mutex);
    state.startup_hidden = true;
    if (state.active) {
        state.active->startup_visible.store(false);
        const auto window = state.active->window.load();
        if (window && !InvalidateRect(window, nullptr, TRUE)) { UnsupportedApi("native_startup_invalidate", GetLastError(), caller); }
    }
    TraceNativeApi("sceSystemServiceHideSplashScreen", 0, caller);
    return 0;
}
} // namespace

NativeVideoLabelReference AcquireNativeVideoBufferLabel(int handle, unsigned index) {
    static_assert(sizeof(std::atomic<std::uint64_t>) == 8 && std::atomic<std::uint64_t>::is_always_lock_free);
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.active || state.stopping_handle || handle != state.handle || index >= state.active->buffer_labels.size()) { return {}; }
    return {reinterpret_cast<std::uint64_t>(&state.active->buffer_labels[index]), state.active};
}
bool CompleteNativePresentationShutdown(int handle) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (!state.active) {
        if (!handle && !state.closed) { return true; } // terminal-log fixture: no output ever existed
        if (!state.closed || state.closed_handle != handle || !state.closed->quit_requested.load()) { return false; }
        state.stopping_handle = handle;
        return !state.closed->window.load() && !state.closed->clock &&
            !state.closed->ready && !state.closed->quit && !state.closed->thread;
    }
    if (handle != state.handle || !state.active->quit_requested.load()) { return false; }
    state.stopping_handle = handle; // deny reopening/registration throughout teardown
    const bool legacy_device = state.graphics != nullptr;
    std::printf("{\"event\":\"native_presentation_shutdown_begin\",\"handle\":%d}\n", handle);
    const auto result = CloseLocked(state, handle, nullptr);
    if (result) { return false; }
    std::printf("{\"event\":\"native_presentation_shutdown_completed\",\"handle\":%d,\"display_clock_stopped\":true,"
                "\"render_fences_drained\":true,\"presentation_detached\":true,\"window_destroyed\":true,"
                "\"window_thread_joined\":true,\"window_handles_closed\":true,\"label_metadata_retained\":true,"
                "\"resident_gpu_context_destroyed\":false,\"legacy_device_destroyed\":%s}\n", handle, legacy_device ? "true" : "false");
    return true;
}
NativeVideoResult WithNativeVideoGpu(int handle, unsigned index, const std::function<void(NativeVideoGpuContext&)>& execute) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    auto* resident = CurrentNativeResidentGpu();
    const auto unavailable = [&](const char* reason, NativeVideoResult result = NativeVideoResult::Unavailable) {
        std::printf("{\"event\":\"native_video_execute_unavailable\",\"reason\":\"%s\",\"handle\":%d,\"buffer\":%u}\n", reason, handle, index);
        return result;
    };
    if (index >= 16) { return unavailable("invalid_port_or_buffer"); }
    if (handle > 0 && handle == state.stopping_handle) { return unavailable("window_shutdown_in_progress", NativeVideoResult::CloseRequested); }
    if (!state.active || handle != state.handle) { return unavailable("invalid_port_or_buffer"); }
    if (state.active->quit_requested.load()) { return unavailable("window_close_requested", NativeVideoResult::CloseRequested); }
    if (!state.graphics && !resident) { return unavailable("gpu_backend_not_created"); }
    for (auto& group : state.groups) {
        if (index < static_cast<unsigned>(group.first) || index - static_cast<unsigned>(group.first) >= group.buffers.size()) { continue; }
        auto& buffer = group.buffers[index - static_cast<unsigned>(group.first)];
        const auto& layout = buffer.surface->layout;
        NativeVideoGpuContext context{state.graphics.get(), layout,
            {static_cast<std::uint8_t*>(buffer.memory.address), static_cast<std::size_t>(layout.bytes)},
            reinterpret_cast<std::uint64_t>(buffer.memory.address), state.active->buffer_labels, resident};
        if (resident) { resident->Run([&] { execute(context); }); }
        else { execute(context); }
        return NativeVideoResult::Completed;
    }
    return unavailable("unregistered_buffer");
}
NativeVideoResult PresentNativeVideoGpu(int handle, unsigned index, unsigned mode, std::int64_t argument,
    NativeVideoPresentTiming* timing) {
    if (timing) { *timing = {}; }
    if (mode != 1) { throw std::runtime_error("Only VSync flip mode is implemented"); }
    const auto present_began=kt::diagnostics::MonotonicMicroseconds();
    auto& state = State(); std::lock_guard lock(state.mutex);
    const auto locked_at=kt::diagnostics::MonotonicMicroseconds();
    const auto unavailable = [&](const char* reason, NativeVideoResult result = NativeVideoResult::Unavailable) {
        std::printf("{\"event\":\"native_video_present_unavailable\",\"reason\":\"%s\",\"handle\":%d,\"buffer\":%u}\n", reason, handle, index); return result;
    };
    auto* resident = CurrentNativeResidentGpu();
    if (index >= 16) { return unavailable("invalid_port_or_buffer"); }
    if (handle > 0 && handle == state.stopping_handle) { return unavailable("window_shutdown_in_progress", NativeVideoResult::CloseRequested); }
    if (!state.active || state.handle != handle) { return unavailable("invalid_port_or_buffer"); }
    if (state.active->quit_requested.load()) { return unavailable("window_close_requested", NativeVideoResult::CloseRequested); }
    if (!state.graphics && !resident) { return unavailable("gpu_backend_not_created"); }
    for (auto& group : state.groups) {
        if (index < static_cast<unsigned>(group.first) || index - static_cast<unsigned>(group.first) >= group.buffers.size()) { continue; }
        auto& buffer = group.buffers[index - static_cast<unsigned>(group.first)];
        const auto& layout = buffer.surface->layout;
        const auto pixels = resident ? std::vector<std::uint8_t>{} : kt::gfx::ReadDisplayPixels(layout, {static_cast<const std::uint8_t*>(buffer.memory.address), static_cast<std::size_t>(layout.bytes)});
        auto& label = state.active->buffer_labels[index];
        label.store(1);
        const auto display_wait_began=kt::diagnostics::MonotonicMicroseconds();
        if (!state.diagnostic_unpaced && !state.active->clock->WaitFlipOpportunity()) { return unavailable("display_clock_stopped"); }
        const auto display_ready=kt::diagnostics::MonotonicMicroseconds();
        state.active->renderer_active.store(true);
        unsigned retries{}, zero_extent_polls{};
        kt::diagnostics::BackendPresentTiming backend_timing;
        const auto began = GetTickCount64();
        for (;;) {
            if (state.active->quit_requested.load()) { return unavailable("window_close_requested_during_present", NativeVideoResult::CloseRequested); }
            const auto result = !state.active->width.load() || !state.active->height.load()
                ? kt::gfx::PresentStatus::ZeroExtent
                : resident ? resident->Present(reinterpret_cast<std::uint64_t>(buffer.memory.address), layout, &backend_timing)
                           : state.graphics->Present(buffer.surface->id, pixels);
            if (result == kt::gfx::PresentStatus::Completed) { break; }
            if (result == kt::gfx::PresentStatus::OutOfDate) {
                if (++retries > 8) { throw std::runtime_error("Swapchain remained out of date after eight recreations"); }
                Sleep(1);
            } else {
                if (!zero_extent_polls++) { std::puts("{\"event\":\"native_video_present_wait\",\"reason\":\"zero_surface_extent\",\"buffer_label_held\":true}"); }
                // The UI thread remains live; restore or close wakes this wait.
                // No label release, flip event, or game-frame success is issued.
                state.active->zero_extent_waiting.store(true, std::memory_order_release);
                const auto wait = WaitForSingleObject(state.active->quit, 25);
                state.active->zero_extent_waiting.store(false, std::memory_order_release);
                if (wait == WAIT_OBJECT_0) { return unavailable("window_close_requested_while_minimized", NativeVideoResult::CloseRequested); }
                if (wait != WAIT_TIMEOUT) { throw std::runtime_error("Window restore/close wait failed"); }
            }
        }
        if (retries || zero_extent_polls) {
            std::printf("{\"event\":\"native_video_present_recovered\",\"swapchain_retries\":%u,\"zero_extent_polls\":%u,\"elapsed_ms\":%llu,\"fence_completed\":true}\n", retries, zero_extent_polls, GetTickCount64() - began);
        }
        const auto backend_completed=kt::diagnostics::MonotonicMicroseconds();
        label.store(0);
        state.active->clock->NotifyFlipCompleted(argument);
        const auto completed_at=kt::diagnostics::MonotonicMicroseconds();
        if (timing) {
            *timing = {true, completed_at-present_began, locked_at-present_began,
                display_wait_began-locked_at, display_ready-display_wait_began,
                backend_completed-display_ready, completed_at-backend_completed, {}};
            // A successful final retry does not describe the earlier waits.
            if (!retries && !zero_extent_polls) { timing->backend = backend_timing; }
        }
        if(completed_at-present_began>=20000) {
            // Wall times for a completed slow present, not GPU timestamps or
            // physical scanout. The backend interval also includes its worker
            // handoff and any resize/minimize recovery; never silently remove
            // synchronization based on this combined interval alone.
            NativeLogf("{\"event\":\"native_video_present_timing\",\"began_clock_us\":%llu,\"clock_us\":%llu,\"duration_us\":%llu,\"thread\":%lu,\"handle\":%d,\"buffer\":%u,\"lock_wait_us\":%llu,\"prepare_us\":%llu,\"display_clock_wait_us\":%llu,\"backend_present_us\":%llu,\"completion_us\":%llu,\"swapchain_retries\":%u,\"zero_extent_polls\":%u}\n",
                present_began,completed_at,completed_at-present_began,GetCurrentThreadId(),handle,index,
                locked_at-present_began,display_wait_began-locked_at,display_ready-display_wait_began,
                backend_completed-display_ready,completed_at-backend_completed,retries,zero_extent_polls);
        }
        return NativeVideoResult::Completed;
    }
    return unavailable("unregistered_buffer");
}
std::uint64_t ResolveVideoImport(std::string_view identifier, std::string_view library, std::string_view module) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (library == "libSceSystemService" && module == "libSceSystemService" && hash == "Vo5V8KAwCmk") {
        return reinterpret_cast<std::uint64_t>(&HideStartup);
    }
    if (library != "libSceVideoOut" || module != "libSceVideoOut") { return 0; }
    if (hash == "Up36PTk687E") { return reinterpret_cast<std::uint64_t>(&VideoOpen); }
    if (hash == "uquVH4-Du78") { return reinterpret_cast<std::uint64_t>(&VideoClose); }
    if (hash == "6kPnj51T62Y") { return reinterpret_cast<std::uint64_t>(&VideoResolution); }
    if (hash == "i6-sR91Wt-4") { return reinterpret_cast<std::uint64_t>(&SetBufferAttribute); }
    if (hash == "w3BY+tAEiQY") { return reinterpret_cast<std::uint64_t>(&VideoRegister); }
    if (hash == "N5KDtkIjjJ4") { return reinterpret_cast<std::uint64_t>(&VideoUnregister); }
    if (hash == "CBiu4mCE1DA") { return reinterpret_cast<std::uint64_t>(&SetFlipRate); }
    if (hash == "1FZBKy8HeNU") { return reinterpret_cast<std::uint64_t>(&VblankStatus); }
    if (hash == "j6RaAUlaLv0") { return reinterpret_cast<std::uint64_t>(&WaitVblank); }
    if (hash == "HXzjK9yI30k") { return reinterpret_cast<std::uint64_t>(&AddFlipEvent); }
    if (hash == "Xru92wHJRmg") { return reinterpret_cast<std::uint64_t>(&AddVblankEvent); }
    if (hash == "-Ozn0F1AFRg") { return reinterpret_cast<std::uint64_t>(&DeleteFlipEvent); }
    if (hash == "oNOQn3knW6s") { return reinterpret_cast<std::uint64_t>(&DeleteVblankEvent); }
    if (hash == "U2JJtSqNKZI") { return reinterpret_cast<std::uint64_t>(&GetVideoEventId); }
    if (hash == "rWUTcKdkUzQ") { return reinterpret_cast<std::uint64_t>(&GetVideoEventData); }
    if (hash == "Mt4QHHkxkOc") { return reinterpret_cast<std::uint64_t>(&GetVideoEventCount); }
    return 0;
}

void ConfigureNativeDisplay(NativeDisplaySettings settings) {
    if (settings.title.empty() || settings.title.size() > 256 || settings.title.find(L'\0') != settings.title.npos)
        throw std::runtime_error("Invalid game window title");
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.active || state.graphics || state.stopping_handle) throw std::runtime_error("Display configuration requires a restart");
    state.display = std::move(settings);
}
NativeDisplaySettings GetNativeDisplaySettings() {
    auto& state = State(); std::lock_guard lock(state.mutex);
    return state.display;
}
void ConfigureNativeUnpacedPresentation(bool enabled) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.active || state.graphics || state.stopping_handle) throw std::runtime_error("Pacing diagnostics require a restart");
    if (enabled && state.display.vsync) throw std::runtime_error("Unpaced presentation requires host vsync=false");
    state.diagnostic_unpaced = enabled;
    std::printf("{\"event\":\"native_presentation_pacing\",\"diagnostic_unpaced\":%s,\"software_flip_wait\":%s,\"guest_vblank_clock_preserved\":true,\"gpu_fences_preserved\":true}\n",
        enabled ? "true" : "false", enabled ? "false" : "true");
}
bool VideoSelfTest() {
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"video_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    check("timeline_marker_only_on_first_f8_keydown", IsTimelineMarkerKey(WM_KEYDOWN, VK_F8, 0) &&
        !IsTimelineMarkerKey(WM_KEYDOWN, VK_F8, 1ll << 30) && !IsTimelineMarkerKey(WM_KEYUP, VK_F8, 0) &&
        !IsTimelineMarkerKey(WM_SYSKEYDOWN, VK_F8, 0) && !IsTimelineMarkerKey(WM_KEYDOWN, 'X', 0));
    RecordTimelineMarker();
    RecordTimelineMarker();
    Resolution resolution{31, 32, 33, 34, 35, 36.0f, 37, 38, {39, 40, 41}, 42};
    check("invalid_handles_preserve_output", Close(1, nullptr) == kHandle && ResolutionStatus(1, &resolution) == kHandle && resolution.full_width == 31);
    check("parameters_and_null_output", Open(0xff, 1, 0, nullptr, false, nullptr) == kValue && Open(0xff, 0, 1, nullptr, false, nullptr) == kValue &&
        Open(-1, 0, 0, nullptr, false, nullptr) == kValue && ResolutionStatus(1, nullptr) == kAddress);
    const auto handle = Open(0xff, 0, 0, nullptr, false, nullptr);
    check("opens_real_owned_window", handle > 0 && State().active && State().active->window.load() && IsWindow(State().active->window.load()));
    if (handle <= 0) { return false; }
    check("startup_overlay_hide_is_idempotent", State().active->startup_visible.load() && HideStartup() == 0 &&
          HideStartup() == 0 && State().startup_hidden && !State().active->startup_visible.load());
    check("one_port_and_resolution_layout", Open(0xff, 0, 0, nullptr, false, nullptr) == kBusy && ResolutionStatus(handle, &resolution) == 0 &&
        resolution.full_width == 1280 && resolution.full_height == 720 && resolution.pane_width == 1280 && resolution.refresh == 0 && resolution.reserved1[2] == 0);
    const auto old_window = State().active->window.load();
    SendMessageW(old_window,WM_CLOSE,0,0);
    bool executed=false;
    check("window_close_keeps_resources_alive_and_rejects_new_execution",State().active->quit_requested.load() &&
        IsWindow(old_window) && WithNativeVideoGpu(handle,0,[&](auto&){executed=true;}) == NativeVideoResult::CloseRequested && !executed);
    check("requested_close_is_distinct_from_invalid_handle_or_buffer",
        WithNativeVideoGpu(handle+1,0,[&](auto&){executed=true;}) == NativeVideoResult::Unavailable &&
        WithNativeVideoGpu(handle,16,[&](auto&){executed=true;}) == NativeVideoResult::Unavailable && !executed);
    State().active->buffer_labels[0].store(7);
    check("cancelled_present_preserves_busy_buffer_label",
        PresentNativeVideoGpu(handle,0,1,73) == NativeVideoResult::CloseRequested &&
        State().active->buffer_labels[0].load() == 7);
    check("invalid_present_is_not_reclassified_as_user_close",
        PresentNativeVideoGpu(handle+1,0,1,73) == NativeVideoResult::Unavailable &&
        PresentNativeVideoGpu(handle,16,1,73) == NativeVideoResult::Unavailable &&
        State().active->buffer_labels[0].load() == 7);
    check("gnm_cancel_releases_submission_scopes_without_completing_frame", GnmCloseSelfTest(handle));
    check("close_joins_and_destroys_window", Close(handle, nullptr) == 0 && !IsWindow(old_window) && !State().active && Close(handle, nullptr) == kHandle);
    const auto reopened = Open(0xff, 0, 0, nullptr, false, nullptr);
    check("reopen_has_new_handle", reopened > handle && ResolutionStatus(handle, &resolution) == kHandle && ResolutionStatus(reopened, &resolution) == 0 && Close(reopened, nullptr) == 0);
    const auto closing = Open(0xff, 0, 0, nullptr, false, nullptr);
    check("process_shutdown_requires_the_matching_requested_close", closing > reopened &&
        !CompleteNativePresentationShutdown(closing) && !CompleteNativePresentationShutdown(closing + 1) && State().active);
    if (closing > 0 && State().active) {
        const auto closing_window = State().active->window.load();
        const auto clock = State().active->clock;
        const std::weak_ptr<WindowJob> metadata = State().active;
        State().active->buffer_labels[0].store(7);
        SendMessageW(closing_window, WM_CLOSE, 0, 0);
        std::array<bool, 2> results{};
        std::thread first([&] { results[0] = CompleteNativePresentationShutdown(closing); });
        std::thread second([&] { results[1] = CompleteNativePresentationShutdown(closing); });
        first.join(); second.join();
        const auto retired = metadata.lock();
        check("concurrent_process_shutdown_joins_once_and_releases_window_resources",
            results[0] && results[1] && !State().active && !IsWindow(closing_window) &&
            !clock->WaitVblank() && retired && !retired->clock && !retired->window.load() &&
            !retired->thread && !retired->ready && !retired->quit);
        check("retired_labels_stay_valid_without_faking_flip_completion", retired &&
            retired->buffer_labels[0].load() == 7 &&
            WithNativeVideoGpu(closing,0,[&](auto&){executed=true;}) == NativeVideoResult::CloseRequested &&
            PresentNativeVideoGpu(closing,0,1,73) == NativeVideoResult::CloseRequested && !executed &&
            !AcquireNativeVideoBufferLabel(closing,0).owner);
        check("process_shutdown_is_idempotent_and_prevents_reopening", CompleteNativePresentationShutdown(closing) &&
            Open(0xff,0,0,nullptr,false,nullptr) == kBusy && !CompleteNativePresentationShutdown(closing + 1));
    }
    std::printf("{\"event\":\"video_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}

bool GraphicsSelfTest(bool interactive) {
    using Allocate = int(KT_SYSV*)(std::int64_t, std::int64_t, std::uint64_t, std::uint64_t, int, std::int64_t*);
    using Map = int(KT_SYSV*)(void**, std::uint64_t, int, int, std::int64_t, std::uint64_t);
    using Release = int(KT_SYSV*)(std::uint64_t, std::uint64_t);
    const auto allocate = reinterpret_cast<Allocate>(ResolveMemoryImport("rTXw65xmLIA"));
    const auto map = reinterpret_cast<Map>(ResolveMemoryImport("L-Q3LEjIbgA"));
    const auto release = reinterpret_cast<Release>(ResolveMemoryImport("MBuItvba6z8"));
    const auto handle = Open(0xff, 0, 0, nullptr, interactive, nullptr);
    if (handle <= 0) { return false; }
    std::int64_t physical = -1;
    bool allocated = false;
    bool passed = true;
    auto check = [&](const char* name, bool result) {
        passed &= result;
        std::printf("{\"event\":\"graphics_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    try {
        allocated = allocate(0, static_cast<std::int64_t>(kNativeProfile.direct_bytes), 0x20000, 0x200000, 3, &physical) == 0;
        void* memory{};
        if (!allocated || map(&memory, 0x20000, 0x33, 0, physical, 0x200000)) { throw std::runtime_error("Graphics fixture memory allocation failed"); }
        auto* bytes = static_cast<unsigned char*>(memory);
        std::array<void*, 2> addresses{bytes, bytes + 32768};
        BufferAttribute attribute{0x80000000u, 0, 0, 128, 64, 128, 0, 0, 0};
        check("invalid_parameters_do_not_create_gpu_resources", RegisterBuffers(handle, -1, addresses.data(), 2, &attribute, nullptr) == kValue &&
            RegisterBuffers(handle, 0, nullptr, 2, &attribute, nullptr) == kAddress && !State().graphics);
        auto bad_attribute = attribute;
        bad_attribute.height = 128;
        void* short_range = bytes + 0x18000;
        check("padded_range_validated_before_gpu_allocation", RegisterBuffers(handle, 0, &short_range, 1, &bad_attribute, nullptr) == static_cast<int>(0x80290013u) && !State().graphics);
        const auto group = RegisterBuffers(handle, 0, addresses.data(), 2, &attribute, nullptr);
        check("real_vulkan_images_registered", group == 0 && State().graphics && State().graphics->ImageCount() == 2);
        if (group < 0) { throw std::runtime_error("Could not register graphics fixture"); }
        passed &= kt::gfx::RasterSelfTest(*State().graphics);
        passed &= kt::gfx::AttachmentSelfTest(*State().graphics);
        passed &= GpuAdapterSelfTest(*State().graphics);
        std::fill(bytes, bytes + 32768, static_cast<unsigned char>(0));
        check("real_presentation_completes_before_buffer_label_is_released", PresentNativeVideoGpu(handle, 0, 1, 73) == NativeVideoResult::Completed &&
            State().active->buffer_labels[0].load() == 0 && State().active->renderer_active.load());
        if (interactive) {
            const auto began = GetTickCount64(); unsigned frames{};
            const auto layout = State().groups[static_cast<unsigned>(group)].buffers[0].surface->layout;
            std::vector<std::uint8_t> pixels(128 * 64 * 4);
            std::puts("{\"event\":\"native_present_demo\",\"content\":\"synthetic_checkerboard\",\"duration_seconds\":60,\"controls\":\"F11/fullscreen; minimize/restore; close to finish\"}");
            while (GetTickCount64() - began < 60000 && !State().active->quit_requested.load()) {
                for (unsigned y = 0; y < 64; ++y) { for (unsigned x = 0; x < 128; ++x) {
                    const auto pixel = (y * 128 + x) * 4; const bool light = ((x / 8) ^ (y / 8)) & 1;
                    pixels[pixel] = light ? 224 : 32; pixels[pixel + 1] = static_cast<std::uint8_t>(frames); pixels[pixel + 2] = light ? 32 : 224; pixels[pixel + 3] = 255;
                } }
                kt::gfx::WriteDisplayPixels(layout, pixels, {bytes, static_cast<std::size_t>(layout.bytes)});
                const auto result = PresentNativeVideoGpu(handle, 0, 1, frames);
                if (result == NativeVideoResult::CloseRequested) { break; }
                if (result != NativeVideoResult::Completed) { throw std::runtime_error("Present demo lost its live video output"); }
                ++frames; Sleep(16);
            }
            std::printf("{\"event\":\"native_present_demo_completed\",\"frames\":%u,\"quit_requested\":%s}\n", frames, State().active->quit_requested.load() ? "true" : "false");
        }
        check("occupied_slots_preserve_registry", RegisterBuffers(handle, 1, addresses.data(), 1, &attribute, nullptr) == static_cast<int>(0x80290010u) && State().graphics->ImageCount() == 2);
        bool injected = false;
        std::array<void*, 2> fresh{bytes + 65536, bytes + 98304};
        try { (void)RegisterBuffers(handle, 4, fresh.data(), 2, &attribute, nullptr, 1); }
        catch (const std::bad_alloc&) { injected = true; }
        check("partial_image_creation_rolls_back", injected && State().graphics->ImageCount() == 2 && State().groups[1].buffers.empty());
        const auto alias = RegisterBuffers(handle, 2, addresses.data(), 2, &attribute, nullptr);
        check("exact_aliases_share_gpu_image", alias == 1 && State().graphics->ImageCount() == 2);
        check("unregister_retains_other_group_aliases", UnregisterBuffers(handle, group) == 0 && State().graphics->ImageCount() == 2 &&
            UnregisterBuffers(handle, alias) == 0 && State().graphics->ImageCount() == 0 && State().surface_cache.empty());
        const auto last = RegisterBuffers(handle, 14, fresh.data(), 2, &attribute, nullptr);
        if (!interactive && last == 0) {
            const auto job = State().active;
            const auto window = job->window.load();
            SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            const bool minimized = IsIconic(window) != 0;
            bool close_while_waiting{};
            std::jthread closer([&] {
                const auto deadline = GetTickCount64() + 3000;
                while (!job->zero_extent_waiting.load(std::memory_order_acquire) && GetTickCount64() < deadline) { Sleep(0); }
                close_while_waiting = job->zero_extent_waiting.load(std::memory_order_acquire);
                SendMessageW(window, WM_CLOSE, 0, 0);
            });
            const auto result = PresentNativeVideoGpu(handle, 14, 1, 74);
            closer.join();
            check("minimized_close_wakes_actual_present_wait_without_completing_flip", minimized && close_while_waiting &&
                result == NativeVideoResult::CloseRequested && job->buffer_labels[14].load() == 1);
        }
        const bool closed = interactive ? Close(handle, nullptr) == 0 : CompleteNativePresentationShutdown(handle);
        check("close_drains_images_before_destroying_window", last == 0 && closed && !State().graphics &&
            release(static_cast<std::uint64_t>(physical), 0x20000) == 0);
        allocated = false;
    } catch (...) {
        if (State().active) { (void)Close(handle, nullptr); }
        if (allocated) { (void)release(static_cast<std::uint64_t>(physical), 0x20000); }
        throw;
    }
    std::printf("{\"event\":\"graphics_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}

int VideoDemo() {
    const auto handle = VideoOpen(0xff, 0, 0, nullptr);
    if (handle <= 0) { return 1; }
    std::shared_ptr<WindowJob> job;
    { auto& state = State(); std::lock_guard lock(state.mutex); job = state.active; }
    std::puts("{\"event\":\"native_video_demo\",\"renderer\":false,\"controls\":\"Alt+Enter or F11; close window to finish\"}");
    std::fflush(stdout);
    const auto quit = WaitForSingleObject(job->quit, 120000);
    const auto closed = VideoClose(handle);
    return quit == WAIT_OBJECT_0 && closed == 0 ? 0 : 1;
}
