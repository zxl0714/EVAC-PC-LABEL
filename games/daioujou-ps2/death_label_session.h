#pragma once
#include "common/death_label_host.h"
#include "ps2_runtime.h"
#include <stdexcept>

namespace cave::daioujou_ps2 {

// A hosted session runs the original Death Label program until it finishes
// the results sequence and reopens its top-level menu. Explicit EXIT keeps
// using the original LoadExecPS2 request, which the caller can route home.
class DeathLabelSession {
public:
    explicit DeathLabelSession(PS2Runtime& runtime, const DeathLabelActions* actions = nullptr)
        : runtime_(runtime), original_(runtime.lookupFunction(FrameEntry)),
          initOptions_(runtime.lookupFunction(InitOptionsEntry)), actions_(actions) {
        if (active_ || !original_ || !initOptions_ || !runtime_.replaceFunction(FrameEntry, &Frame))
            throw std::runtime_error("Cannot start hosted Death Label");
        if (!runtime_.replaceFunction(InitOptionsEntry, &InitOptions)) {
            runtime_.replaceFunction(FrameEntry, original_);
            throw std::runtime_error("Cannot set hosted Death Label defaults");
        }
        if (actions_) {
            consumeInput_ = runtime_.lookupFunction(ConsumeInputEntry);
            if (!consumeInput_ || !runtime_.replaceFunction(ConsumeInputEntry, &ConsumeInput)) {
                runtime_.replaceFunction(InitOptionsEntry, initOptions_);
                runtime_.replaceFunction(FrameEntry, original_);
                throw std::runtime_error("Cannot bind hosted Death Label controls");
            }
        }
        active_ = this;
    }
    ~DeathLabelSession() {
        if (consumeInput_) runtime_.replaceFunction(ConsumeInputEntry, consumeInput_);
        runtime_.replaceFunction(InitOptionsEntry, initOptions_);
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
    static constexpr uint32_t InitOptionsEntry = 0x238160, MapDisplay = GuestGp - 0x5d88;
    static constexpr uint32_t ConsumeInputEntry = 0x16b450, ConsumeInputReturn = 0x1afd98;
    static constexpr uint32_t PlayerOnePad = 0x61ad3c, ArcadeInput = 0x290818;

    static void InitOptions(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        active_->initOptions_(memory,ctx,runtime);
        // The original option remains editable in the pause menu and saved
        // settings are still applied by the memory-card loader.
        memory[MapDisplay] = 0;
    }

    static void ConsumeInput(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        const auto& session = *active_;
        if (getRegU32(ctx,31) == ConsumeInputReturn && getRegU32(ctx,28) == GuestGp &&
            memory[Playing] && !memory[Menu]) {
            // The pad reader has already sampled the host once this frame.
            // DEATH.ELF stores its raw pad bytes in packet order and feeds
            // active-low A/B/C to the arcade game through this input byte.
            const unsigned buttons = (unsigned(memory[PlayerOnePad]) << 8) | memory[PlayerOnePad + 1];
            const auto& actions = *session.actions_;
            unsigned mapped = memory[ArcadeInput] | 0xe0;
            if (buttons & actions.shot) mapped &= ~0x20u;
            if (buttons & actions.bomb) mapped &= ~0x40u;
            if (buttons & actions.rapid) mapped &= ~0x80u;
            memory[ArcadeInput] = static_cast<std::uint8_t>(mapped);
        }
        session.consumeInput_(memory,ctx,runtime);
    }

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
    PS2Runtime::RecompiledFunction initOptions_;
    const DeathLabelActions* actions_{};
    PS2Runtime::RecompiledFunction consumeInput_{};
    bool played_{}, completed_{};
};

}
