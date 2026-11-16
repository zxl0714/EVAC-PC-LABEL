#pragma once
#include "common/death_label_host.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <atomic>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <utility>

namespace cave::daioujou_ps2 {

// A hosted session runs the original Death Label program until it finishes
// the results sequence and reopens its top-level menu. Explicit EXIT keeps
// using the original LoadExecPS2 request, which the caller can route home.
class DeathLabelSession {
public:
    using ObserveFrame = std::function<void(const uint8_t*,bool,bool)>;

    explicit DeathLabelSession(PS2Runtime& runtime, const DeathLabelActions* actions = nullptr,
                               ObserveFrame observe = {}, std::atomic_uint64_t* firstVisibleField = nullptr)
        : runtime_(runtime), original_(runtime.lookupFunction(FrameEntry)),
          initOptions_(runtime.lookupFunction(InitOptionsEntry)), actions_(actions), observe_(std::move(observe)),
          firstVisibleField_(firstVisibleField) {
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
            rootMenu_ = runtime_.lookupFunction(RootMenuEntry);
            if (!rootMenu_ || !runtime_.replaceFunction(RootMenuEntry, &StartFromRootMenu)) {
                runtime_.replaceFunction(ConsumeInputEntry, consumeInput_);
                runtime_.replaceFunction(InitOptionsEntry, initOptions_);
                runtime_.replaceFunction(FrameEntry, original_);
                throw std::runtime_error("Cannot bind hosted Death Label entry");
            }
        }
        active_ = this;
    }
    ~DeathLabelSession() {
        if (rootMenu_) runtime_.replaceFunction(RootMenuEntry, rootMenu_);
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
    static constexpr uint32_t Scene = GuestGp - 0x54a8;
    static constexpr uint8_t FighterSelectScene = 19;
    static constexpr uint64_t StartupFieldLimit = 60 * 60;
    static constexpr uint32_t Menu = GuestGp - 0x59b0;
    static constexpr uint32_t MenuPage = GuestGp - 0x5920;
    static constexpr uint32_t InitOptionsEntry = 0x238160, MapDisplay = GuestGp - 0x5d88;
    static constexpr uint32_t ConsumeInputEntry = 0x16b450, ConsumeInputReturn = 0x1afd98;
    static constexpr uint32_t PlayerOnePad = 0x61ad3c, ArcadeInput = 0x290818;
    static constexpr uint32_t RootMenuEntry = 0x1b57d0, RootMenuReturn = 0x1b570c;
    static constexpr uint32_t MenuCursor = GuestGp - 0x5950;
    static constexpr uint32_t PlayerOneMenuButtons = 0x61acf0, MenuConfirmEdge = 0x61acf8;

    static void StartFromRootMenu(uint8_t* memory, R5900Context* ctx, PS2Runtime* runtime) {
        auto& session = *active_;
        if (!session.directStart_ && getRegU32(ctx,31) == RootMenuReturn &&
            getRegU32(ctx,28) == GuestGp && memory[Menu] == 1 && memory[MenuPage] == 0) {
            // Start through the original confirmation path. This initializes
            // the stage and fighter-select screens with the loaded options.
            memory[MenuCursor] = 1;
            constexpr uint8_t confirm = 0x20;
            const auto held = std::exchange(memory[PlayerOneMenuButtons], confirm);
            const auto pressed = std::exchange(memory[MenuConfirmEdge], confirm);
            session.rootMenu_(memory,ctx,runtime);
            memory[MenuConfirmEdge] = pressed;
            memory[PlayerOneMenuButtons] = held;
            session.directStart_ = memory[Menu] == 4;
            return;
        }
        session.rootMenu_(memory,ctx,runtime);
    }

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
            if (session.firstVisibleField_) {
                const auto tick=runtime->eeScheduler().currentVSyncTick();
                // The scene switches once the root menu has faded out. Let
                // the selection draw reach the GS display page before showing it.
                if (session.directStart_ && menu == 0 && memory[Scene] == FighterSelectScene) {
                    session.firstVisibleField_->store(tick+2,std::memory_order_release);
                    session.firstVisibleField_ = nullptr;
                    std::cerr<<"[ps2-death-label-entry] fighter_select_field="<<tick+2<<'\n';
                } else if (tick >= StartupFieldLimit) {
                    // The player cannot act on a startup screen that the host hides.
                    std::cerr<<"[ps2-death-label-entry] fighter selection did not open\n";
                    runtime->requestStop();
                    return;
                }
            }
            if (session.observe_) session.observe_(memory,playing,menu != 0);
            session.played_ |= playing && menu == 0;
            if ((session.played_ || session.directStart_) && !playing && menu == 1 && memory[MenuPage] == 0) {
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
    PS2Runtime::RecompiledFunction rootMenu_{};
    ObserveFrame observe_;
    std::atomic_uint64_t* firstVisibleField_{};
    bool played_{}, directStart_{}, completed_{};
};

}
