#include "futari_init.h"
#include "build_metadata.h"
#include "host.h"
#include "community_runtime.h"
#include "audio_guest_trace.h"
#include "audio_diagnostics.h"
#include "audio_wait_policy.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

void FutariRegisterModuleAliases();
class FutariApp final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<FutariApp>(new FutariApp(context,"futari",PPCImageConfig,futari::kBuildIdentity));
    }
private:
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {
        auto endpoint=run().settings.endpoint;
        while(!endpoint.empty() && endpoint.back()=='/') endpoint.pop_back();
        endpoint+="/games/futari";
        REXLOG_INFO("Futari community bridge installed; game=futari protocol=1 existing replay/outbox format");
        return std::make_shared<FutariCommunity>(endpoint,run().settings.nickname,run().source_build,run().profile);
    }
    std::string NativeNickname(std::string name) override {return FutariNativeNickname(std::move(name));}
    void OnGameSetup() override {
        if (!run().content_source.empty())
            runtime()->kernel_state()->content_manager()->SetReadOnlyContentRoot(run().content_source);
        FutariStartAudioWaitPolicy();FutariRegisterModuleAliases();
        audio_marker_=FutariCreateAudioMarker(window());FutariStartAudioGuestTrace();
    }
    void OnGameStop() override {
        FutariStopAudioWaitPolicy();FutariStopAudioGuestTrace();
        if (audio_marker_) {window()->RemoveInputListener(audio_marker_.get());audio_marker_.reset();}
    }
    std::unique_ptr<rex::ui::WindowInputListener> audio_marker_;
};
REX_DEFINE_APP(futari, FutariApp::Create)
