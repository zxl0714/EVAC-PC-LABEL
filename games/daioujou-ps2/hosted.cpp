#include "common/death_label_host.h"
#include "backends/ps2/host_input.h"
#include "disc_program.h"
#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace {
class HostedPad final : public cave::ps2::HostPadSource {
public:
    explicit HostedPad(const cave::DeathLabelHost& host) : host_(host) {}
    cave::ps2::HostPadState Read(unsigned port) override {
        cave::ps2::HostPadState state;
        uint32_t buttons{};
        host_.readPad(host_.padContext,port,&buttons,state.axes.data());
        state.buttons=cave::ps2::Ps2Buttons(buttons);
        return state;
    }
private:
    const cave::DeathLabelHost& host_;
};
}

extern "C" __declspec(dllexport) cave::DeathLabelResult EvacRunDeathLabel(const cave::DeathLabelHost* request) noexcept {
    using cave::DeathLabelResult;
    namespace fs = std::filesystem;
    try {
        static std::mutex session;
        std::unique_lock lease(session,std::try_to_lock);
        if (!lease || !request || request->version != cave::DeathLabelHostVersion || !request->window ||
            !request->closeEvent || !request->prepared || !request->disc || !request->profile ||
            !request->readPad || !std::isfinite(request->volume) || request->volume < 0 || request->volume > 1)
            return DeathLabelResult::Failed;
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
        cave::ps2::NativeHost host;
        cave::ps2::NativeHostOptions options;
        options.existingWindow=request->window;
        options.closeEvent=request->closeEvent;
        options.existingPad=std::make_shared<HostedPad>(*request);
        options.vsync=request->vsync;
        options.reflex=request->reflex;
        options.amdAntiLag=request->amdAntiLag;
        options.volume=request->volume;
        host.Configure(std::move(options));
        host.Initialize("DoDonPachi DaiOuJou | Death Label");
        const auto result = cave::daioujou_ps2::RunDiscProgram(paths,host,true,true,{},request->actions);
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
