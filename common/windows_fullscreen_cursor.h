#pragma once
#include <Windows.h>

namespace cave {
inline constexpr UINT kFullscreenCursorIdleMs = 2000;

// Owned and called by the game window's message thread.
class FullscreenCursor {
public:
    void FullscreenChanged(HWND window, bool fullscreen) {
        Activate(window, fullscreen && GetForegroundWindow() == window);
    }

    bool Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam, bool fullscreen) {
        switch (message) {
        case WM_ACTIVATE:
            Activate(window, fullscreen && LOWORD(wparam) != WA_INACTIVE && !HIWORD(wparam));
            break;
        case WM_DESTROY:
            Activate(window, false);
            break;
        case WM_SETCURSOR:
            if (hidden_ && LOWORD(lparam) == HTCLIENT) {
                SetCursor(nullptr);
                return true;
            }
            break;
        case WM_MOUSEMOVE:
            if (active_) {
                SetHidden(window, false);
                last_motion_ = GetTickCount64();
                if (!armed_) Arm(window, kFullscreenCursorIdleMs);
            }
            break;
        case WM_TIMER:
            if (wparam != TimerId()) break;
            if (armed_) {
                const auto elapsed = GetTickCount64() - last_motion_;
                if (elapsed < kFullscreenCursorIdleMs) {
                    Arm(window, UINT(kFullscreenCursorIdleMs - elapsed));
                } else {
                    Stop(window);
                    SetHidden(window, active_ && GetForegroundWindow() == window);
                }
            }
            return true;
        }
        return false;
    }

private:
    UINT_PTR TimerId() const { return reinterpret_cast<UINT_PTR>(this); }

    void Arm(HWND window, UINT delay) {
        if (SetTimer(window, TimerId(), delay, nullptr)) armed_ = true;
    }

    void Stop(HWND window) {
        if (armed_) KillTimer(window, TimerId());
        armed_ = false;
    }

    void Activate(HWND window, bool active) {
        if (active_ == active) return;
        active_ = active;
        Stop(window);
        SetHidden(window, active);
    }

    void SetHidden(HWND window, bool hidden) {
        if (hidden_ == hidden) return;
        hidden_ = hidden;
        POINT pointer{};
        if (GetCursorPos(&pointer) && WindowFromPoint(pointer) == window) {
            SetCursor(hidden ? nullptr : reinterpret_cast<HCURSOR>(GetClassLongPtrW(window, GCLP_HCURSOR)));
        }
    }

    ULONGLONG last_motion_{};
    bool active_{}, hidden_{}, armed_{};
};
}
