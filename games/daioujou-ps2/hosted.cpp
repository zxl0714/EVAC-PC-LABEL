#include "common/death_label_host.h"
#include "backends/ps2/host_input.h"
#include "disc_program.h"
#include "death_label_gadgets.h"
#include <Windows.h>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

void cave::daioujou_ps2::ObserveDeathLabelDamage(const std::uint8_t* memory, const R5900Context* context,
                                                DeathLabelAttack attack, std::uint32_t operands) {
    DeathLabelGadgetFeed::RecordDamage(memory,context,attack,operands);
}

namespace {
using cave::DeathLabelHost;
using cave::DeathLabelResult;
namespace fs = std::filesystem;

bool ValidScreen(const cave::DeathLabelScreen& screen) {
    return screen.width>0 && screen.width<=4096 && screen.height>0 && screen.height<=4096 &&
        screen.offsetX>=-1920 && screen.offsetX<=1920 && screen.offsetY>=-1080 && screen.offsetY<=1080 &&
        screen.quarterTurns<=3;
}

cave::ps2::GsPresentation PresentationFor(const cave::DeathLabelScreen& screen) {
    // The upright Death Label GS scanout includes black side margins and
    // room for the optional map. Match the playfield inside it to the PS4 rect.
    constexpr float x=178.0f/960.0f,y=0,width=588.0f/960.0f,height=718.0f/720.0f;
    constexpr float source[][4]{{x,y,width,height},{1-y-height,x,height,width},
                               {1-x-width,1-y-height,width,height},{y,1-x-width,height,width}};
    const auto& bounds=source[screen.quarterTurns];
    const float targetWidth=screen.width/1920.0f,targetHeight=screen.height/1080.0f;
    const float left=0.5f+screen.offsetX/1920.0f-targetWidth/2;
    const float top=0.5f+screen.offsetY/1080.0f-targetHeight/2;
    const float scanoutWidth=targetWidth/bounds[2],scanoutHeight=targetHeight/bounds[3];
    return {16.0f/9.0f,left-bounds[0]*scanoutWidth,top-bounds[1]*scanoutHeight,
            scanoutWidth,scanoutHeight,screen.quarterTurns};
}

class HostedPad final : public cave::ps2::HostPadSource {
public:
    HostedPad(const cave::DeathLabelHost& host,const std::atomic_uint64_t* firstVisibleField)
        : host_(host),firstVisibleField_(firstVisibleField) {}
    cave::ps2::HostPadState Read(unsigned port) override {
        cave::ps2::HostPadState state;
        if(firstVisibleField_ && !firstVisibleField_->load(std::memory_order_acquire)) return state;
        uint32_t buttons{};
        host_.readPad(host_.padContext,port,&buttons,state.axes.data());
        state.buttons=cave::ps2::Ps2Buttons(buttons);
        return state;
    }
private:
    const cave::DeathLabelHost& host_;
    const std::atomic_uint64_t* firstVisibleField_;
};

bool ValidRequest(const DeathLabelHost* request) {
    return request && request->version == cave::DeathLabelHostVersion && request->window &&
        request->closeEvent && request->prepared && request->disc && request->profile &&
        request->readPad && std::isfinite(request->volume) && request->volume >= 0 && request->volume <= 1 &&
        request->audio.music <= 100 && request->audio.effects <= 100 && ValidScreen(request->screen);
}

DeathLabelResult RunHosted(const DeathLabelHost* request, const std::function<bool()>& waitForWindow = {}) noexcept {
    try {
        static std::mutex session;
        std::unique_lock lease(session,std::try_to_lock);
        if (!lease) return DeathLabelResult::Failed;
        const auto prepared = fs::canonical(request->prepared);
        const auto disc = fs::canonical(request->disc);
        const fs::path profile(request->profile);
        if (!profile.is_absolute() || !fs::is_regular_file(disc) || !fs::is_regular_file(prepared/"death.elf"))
            return DeathLabelResult::Failed;
        fs::create_directories(profile/"mc0");
        const auto lock = CreateFileW((profile/".profile.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if (lock==INVALID_HANDLE_VALUE) throw std::runtime_error("The PS2 profile is already in use");
        const std::unique_ptr<void,decltype(&CloseHandle)> profileLock(lock,CloseHandle);
        PS2Runtime::IoPaths paths;
        paths.elfPath=prepared/"death.elf";
        paths.elfDirectory=prepared;
        paths.cdImage=disc;
        paths.cdRoot=prepared;
        paths.hostRoot=prepared;
        paths.mcRoot=profile/"mc0";
        cave::daioujou_ps2::DeathLabelGadgets gadgets(request->screen);
        cave::daioujou_ps2::DeathLabelSession::ObserveFrame observe;
        cave::daioujou_ps2::DeathLabelGadgetFeed* feed{};
        std::atomic_uint64_t firstVisibleField{};
        auto* firstField=request->actions ? &firstVisibleField : nullptr;
        cave::ps2::NativeHost host;
        cave::ps2::NativeHostOptions options;
        options.existingWindow=request->window;
        options.closeEvent=request->closeEvent;
        options.existingPad=std::make_shared<HostedPad>(*request,firstField);
        options.firstVisibleField=firstField;
        if (waitForWindow) {
            options.waitForBorrowedWindow=[&] {
                if (!waitForWindow()) return false;
                gadgets.ResetRun();
                return true;
            };
        }
        options.vsync=request->vsync;
        options.reflex=request->reflex;
        options.amdAntiLag=request->amdAntiLag;
        options.volume=request->volume;
        options.streamVolume=request->audio.music;
        options.voiceVolume=request->audio.effects;
        options.presentation=PresentationFor(request->screen);
        if (request->gadgetSprites && request->screen.gadgets) {
            try {
                options.overlay=gadgets.Load(request->gadgetSprites);
                feed=&gadgets.Feed();
                observe=[&gadgets](const auto* memory,bool playing,bool menu) { gadgets.Observe(memory,playing,menu); };
            } catch (const std::exception& error) {
                std::cerr<<"[ps2-death-label-gadgets] "<<error.what()<<'\n';
            }
        }
        host.Configure(std::move(options));
        host.Initialize("DoDonPachi DaiOuJou | Death Label");
        const auto result = cave::daioujou_ps2::RunDiscProgram(paths,host,true,true,{},request->actions,
            std::move(observe),feed,firstField);
        if (result.completed) return DeathLabelResult::Completed;
        if (result.next=="boot.elf") return DeathLabelResult::Exit;
        return host.Alive()?DeathLabelResult::Failed:DeathLabelResult::Closed;
    } catch (const std::exception& error) {
        std::cerr<<"[ps2-death-label] "<<error.what()<<'\n';
        return DeathLabelResult::Failed;
    } catch (...) {
        return DeathLabelResult::Failed;
    }
}

// The renderer and guest keep the same owner threads across menu handoffs.
// Copy the request before the PS4 caller can leave the stack frame.
class HostedSession final {
public:
    explicit HostedSession(const DeathLabelHost& request)
        : prepared_(request.prepared),disc_(request.disc),profile_(request.profile),
          sprites_(request.gadgetSprites ? request.gadgetSprites : L""),
          actions_(*request.actions),request_(request) {
        request_.prepared=prepared_.c_str();
        request_.disc=disc_.c_str();
        request_.profile=profile_.c_str();
        request_.gadgetSprites=sprites_.empty() ? nullptr : sprites_.c_str();
        request_.actions=&actions_;
        request_.session=nullptr;
    }
    ~HostedSession() {
        if (!owner_.joinable()) return;
        {
            std::lock_guard lock(mutex_);
            if (phase_ != Phase::Finished) phase_=Phase::Stopping;
        }
        changed_.notify_all();
        owner_.join();
    }
    bool Matches(const DeathLabelHost& request) const {
        return request.window == request_.window && request.closeEvent == request_.closeEvent &&
            request.readPad == request_.readPad && request.padContext == request_.padContext &&
            request.volume == request_.volume && request.audio == request_.audio &&
            request.screen == request_.screen && *request.actions == actions_ &&
            request.vsync == request_.vsync && request.reflex == request_.reflex &&
            request.amdAntiLag == request_.amdAntiLag && prepared_ == request.prepared &&
            disc_ == request.disc && profile_ == request.profile &&
            sprites_ == (request.gadgetSprites ? request.gadgetSprites : L"");
    }
    DeathLabelResult Enter() {
        std::unique_lock lock(mutex_);
        phase_=Phase::Running;
        if (!owner_.joinable()) owner_=std::thread([this] { Run(); });
        else changed_.notify_all();
        changed_.wait(lock,[&] { return phase_ != Phase::Running; });
        return phase_ == Phase::Parked ? DeathLabelResult::Completed : result_;
    }
    bool Parked() {
        std::lock_guard lock(mutex_);
        return phase_ == Phase::Parked;
    }

private:
    enum class Phase { Running, Parked, Stopping, Finished };
    bool WaitForWindow() {
        std::unique_lock lock(mutex_);
        phase_=Phase::Parked;
        changed_.notify_all();
        changed_.wait(lock,[&] { return phase_ != Phase::Parked; });
        return phase_ == Phase::Running;
    }
    void Run() {
        const auto result=RunHosted(&request_,[this] { return WaitForWindow(); });
        {
            std::lock_guard lock(mutex_);
            result_=result;
            phase_=Phase::Finished;
        }
        changed_.notify_all();
    }

    const fs::path prepared_,disc_,profile_,sprites_;
    const cave::DeathLabelActions actions_;
    DeathLabelHost request_;
    std::mutex mutex_;
    std::condition_variable changed_;
    Phase phase_{};
    DeathLabelResult result_{DeathLabelResult::Failed};
    std::thread owner_;
};
}

extern "C" __declspec(dllexport) cave::DeathLabelResult EvacRunDeathLabel(const cave::DeathLabelHost* request) noexcept {
    try {
        if (!ValidRequest(request)) return DeathLabelResult::Failed;
        if (!request->session || !request->actions) return RunHosted(request);
        auto& slot=*request->session;
        std::unique_ptr<HostedSession> session(static_cast<HostedSession*>(std::exchange(slot,nullptr)));
        if (session && !session->Matches(*request)) session.reset();
        if (!session) session=std::make_unique<HostedSession>(*request);
        const auto result=session->Enter();
        if (session->Parked()) slot=session.release();
        return result;
    } catch (const std::exception& error) {
        std::cerr<<"[ps2-death-label] "<<error.what()<<'\n';
        return DeathLabelResult::Failed;
    } catch (...) {
        return DeathLabelResult::Failed;
    }
}

extern "C" __declspec(dllexport) void EvacReleaseDeathLabel(void* session) noexcept {
    delete static_cast<HostedSession*>(session);
}
