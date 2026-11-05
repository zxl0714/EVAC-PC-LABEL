#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace cave::ps2::gs_crash {
enum class Stage : uint8_t {
    Init, BetweenFrames, WaitFence, FenceSignaled, FenceUnconfirmed, FlushGs, Acquire,
    Scanout, ReuseImage, EmptyImage, SourceImage, BeginPass, BindProgram,
    Draw, EndPass, Submit, Submitted, Present, Presented, Count
};
inline constexpr auto names=std::to_array<const char*>({
    "init-gs","between-frames","wait-output-fence","output-fence-signaled","output-fence-unconfirmed",
    "flush-gs","acquire-swapchain","scanout","reuse-image","empty-image","source-image",
    "begin-pass","bind-program","draw","end-pass","submit","submit-returned",
    "present","present-returned"});
static_assert(names.size()==static_cast<size_t>(Stage::Count));
static_assert(std::atomic<uint64_t>::is_always_lock_free);

// One writer (the window/GS thread). The exception filter can read these
// slots from any thread, even if the writer stopped in the middle of Mark().
inline std::atomic<DWORD> owner{};
inline std::atomic<uint64_t> published{}, confirmedPlusOne{}, activeGif{};
inline std::array<std::atomic<uint64_t>,32> slots{};
inline void Start() noexcept {
    published.store(0);
    confirmedPlusOne.store(0);
    activeGif.store(0);
    owner.store(GetCurrentThreadId());
}
inline void Mark(Stage stage,uint64_t frame) noexcept {
    const auto serial=published.load(std::memory_order_relaxed)+1;
    const auto entry=(serial<<48)|(uint64_t(uint32_t(frame))<<8)|uint8_t(stage);
    slots[(serial-1)%slots.size()].store(entry,std::memory_order_release);
    if(stage==Stage::FenceSignaled)confirmedPlusOne.store(frame+1,std::memory_order_release);
    published.store(serial,std::memory_order_release);
}
inline void Write(HANDLE file) noexcept {
    const auto renderThread=owner.load(std::memory_order_acquire);
    if(!renderThread)return;
    const auto end=published.load(std::memory_order_acquire);
    const auto confirmed=confirmedPlusOne.load(std::memory_order_acquire);
    char line[160]{};
    auto emit=[&](int n) {
        DWORD written{};
        if(n>0 && size_t(n)<sizeof(line))WriteFile(file,line,DWORD(n),&written,nullptr);
    };
    emit(std::snprintf(line,sizeof(line),"[ps2_graphics]\nrender_thread=%lu\nfault_on_render_thread=%u\n",
         renderThread,unsigned(renderThread==GetCurrentThreadId())));
    if(confirmed)emit(std::snprintf(line,sizeof(line),"last_signaled_output_fence_frame=%llu\n",confirmed-1));
    else emit(std::snprintf(line,sizeof(line),"last_signaled_output_fence_frame=none\n"));
    const auto gif=activeGif.load(std::memory_order_acquire);
    if(gif)emit(std::snprintf(line,sizeof(line),"active_gif_path=%u bytes=%llu\n",unsigned(gif&3u),gif>>2));
    const auto start=end<slots.size()?1:end-slots.size()+1;
    for(auto serial=start;serial<=end;++serial) {
        const auto entry=slots[(serial-1)%slots.size()].load(std::memory_order_acquire);
        const auto stage=uint8_t(entry);
        if(uint16_t(entry>>48)!=uint16_t(serial) || stage>=names.size())continue;
        emit(std::snprintf(line,sizeof(line),"event=%llu frame=%u stage=%s\n",
                           serial,uint32_t(entry>>8),names[stage]));
    }
}
}
