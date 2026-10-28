#pragma once

namespace cave::ps4 {
// Establish a FIFO-capable window presentation path before its first optional
// low-latency mode. Some Windows Vulkan drivers otherwise select a GDI-copy
// path for a window that has only ever had an Immediate/Mailbox swapchain.
// The temporary chain has no acquired images or presentation requests. Its
// owner retires it after successful replacement, or on creation failure.
template<class Info,class Mode,class Create>
auto CreateWindowSwapchain(Info info,bool& established,Mode fifo,Create create) {
    if(established || info.presentMode==fifo) {
        auto chain=create(info);
        established=true;
        return chain;
    }
    auto initial=info;
    initial.presentMode=fifo;
    auto bootstrap=create(initial);
    info.oldSwapchain=*bootstrap;
    auto chain=create(info);
    established=true;
    return chain;
}
}
