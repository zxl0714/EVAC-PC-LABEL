#pragma once
#include "ps2_runtime.h"
#include <stdexcept>

namespace cave::daioujou_ps2 {

// A hosted session runs the original Death Label program until it finishes
// the results sequence and reopens its top-level menu. Explicit EXIT keeps
// using the original LoadExecPS2 request, which the caller can route home.
class DeathLabelSession {
public:
    explicit DeathLabelSession(PS2Runtime& runtime)
        : runtime_(runtime), original_(runtime.lookupFunction(FrameEntry)) {
        if (active_ || !original_ || !runtime_.replaceFunction(FrameEntry, &Frame))
            throw std::runtime_error("Cannot start hosted Death Label");
        active_ = this;
    }
    ~DeathLabelSession() {
        runtime_.replaceFunction(FrameEntry, original_);
        active_ = nullptr;
    }
    DeathLabelSession(const DeathLabelSession&) = delete;
    DeathLabelSession& operator=(const DeathLabelSession&) = delete;

    bool Completed() const { return completed_; }

private:
    // SLPS-25233 DEATH.ELF. The regular frame loop calls this entry after
    // processing the previous field; its earlier startup calls use a
    // different return address. Menu state 1 / page 0 is the root menu.
    static constexpr uint32_t FrameEntry = 0x1afd60, FrameReturn = 0x1e65e8;
    static constexpr uint32_t GuestGp = 0x2965f0;
    static constexpr uint32_t Playing = GuestGp - 0x5cb0;
    static constexpr uint32_t Menu = GuestGp - 0x59b0;
    static constexpr uint32_t MenuPage = GuestGp - 0x5920;

    static void Frame(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        auto& session = *active_;
        if (getRegU32(ctx,31) == FrameReturn && getRegU32(ctx,28) == GuestGp) {
            const bool playing = memory[Playing] != 0;
            const auto menu = memory[Menu];
            session.played_ |= playing && menu == 0;
            if (session.played_ && !playing && menu == 1 && memory[MenuPage] == 0) {
                session.completed_ = true;
                runtime->requestStop();
                return;
            }
        }
        session.original_(memory,ctx,runtime);
    }

    static inline DeathLabelSession* active_{};
    PS2Runtime& runtime_;
    PS2Runtime::RecompiledFunction original_;
    bool played_{}, completed_{};
};

}
