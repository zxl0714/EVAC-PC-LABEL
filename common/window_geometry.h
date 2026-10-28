#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <Windows.h>

namespace cave {
struct ClientExtent { unsigned width,height; bool operator==(const ClientExtent&) const = default; };
// Physical pixels. Keep the requested aspect, including on small/high-DPI screens.
inline ClientExtent FitClientExtent(ClientExtent requested, unsigned available_width, unsigned available_height) {
    if(!requested.width || !requested.height || !available_width || !available_height)
        throw std::runtime_error("Window dimensions must be positive");
    if(requested.width<=available_width && requested.height<=available_height)return requested;
    if(std::uint64_t(available_width)*requested.height<=std::uint64_t(available_height)*requested.width)
        return {available_width,std::max(1u,unsigned(std::uint64_t(requested.height)*available_width/requested.width))};
    return {std::max(1u,unsigned(std::uint64_t(requested.width)*available_height/requested.height)),available_height};
}
inline void FitGameWindow(HWND window, unsigned width, unsigned height) {
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))
        throw std::runtime_error("Cannot query game window monitor");
    RECT border{};
    if(!AdjustWindowRectExForDpi(&border,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
        DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),GetDpiForWindow(window)))
        throw std::runtime_error("Cannot measure game window frame");
    const auto& work=monitor.rcWork;
    const auto available_width=std::max(1L,work.right-work.left-(border.right-border.left)-24);
    const auto available_height=std::max(1L,work.bottom-work.top-(border.bottom-border.top)-24);
    const auto size=FitClientExtent({width,height},unsigned(available_width),unsigned(available_height));
    const auto outer_width=LONG(size.width)+border.right-border.left;
    const auto outer_height=LONG(size.height)+border.bottom-border.top;
    if(!SetWindowPos(window,nullptr,work.left+(work.right-work.left-outer_width)/2,
        work.top+(work.bottom-work.top-outer_height)/2,outer_width,outer_height,SWP_NOZORDER|SWP_NOACTIVATE))
        throw std::runtime_error("Cannot set game window size");
}
}
