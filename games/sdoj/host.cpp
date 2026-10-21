#include "sdoj_init.h"
#include "build_metadata.h"
#include "host.h"
#include "community/sdoj_service.h"
#include "latency/frame_delay.h"
#include "diagnostics/test_pad.h"
#include <rex/cvar.h>
#include <rex/ui/window.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <stdexcept>

REXCVAR_DEFINE_STRING(sdoj_community_test_endpoint,"","Diagnostics","Explicit loopback-only SDOJ integration test provider")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(sdoj_test_pad_file,"","Diagnostics","Explicit offline test pad command file; empty disables")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class SdojApp final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<SdojApp>(new SdojApp(context,"sdoj",PPCImageConfig,sdoj::kBuildIdentity));
    }
private:
    void OnGameSetup() override {
        const auto test_endpoint=rex::cvar::GetFlagByName("sdoj_community_test_endpoint");
        if(!test_endpoint.empty()) {
            if(!test_endpoint.starts_with("http://127.0.0.1:"))
                throw std::runtime_error("SDOJ development provider requires an explicit loopback endpoint");
            test_community_=std::make_shared<sdoj::SdojCommunity>(test_endpoint,run().settings.nickname,run().source_build);
            runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(
                run().settings.nickname,true,NativeNickname(run().settings.nickname));
            rex::system::xam::SetCommunityService(test_community_);
            REXLOG_WARN("SDOJ development community provider enabled; loopback only, capability acceptance remains pending");
        } else if(rex::cvar::GetFlagByName("sdoj_native_trace")=="true") {
            // Explicit development run only: expose the native menu paths so
            // their ABI can be observed. There is no provider or HTTP traffic;
            // native requests report unavailable until the adapter is ready.
            runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(
                run().settings.nickname,true,run().settings.nickname);
            REXLOG_WARN("SDOJ_NATIVE development trace: diagnostic Live sign-in; no provider, no HTTP; not an online acceptance run");
        }
    }
    void OnGameStop() override {
        sdoj::latency::Stop();
        if(test_community_) {
            if(runtime() && runtime()->kernel_state())rex::system::xam::CancelCommunityIo(runtime()->kernel_state());
            rex::system::xam::SetCommunityService({});test_community_->Stop();test_community_.reset();
        }
    }
    void OnGamePreSetup(rex::RuntimeConfig& config) override {
        sdoj::latency::Configure(run().settings.sdoj_frame_delay);
        const auto pad_file=rex::cvar::GetFlagByName("sdoj_test_pad_file");
        if(!pad_file.empty()) {
            if(run().settings.community_enabled)
                throw std::runtime_error("SDOJ test pad requires community.enabled=false");
            sdoj::diagnostics::AddTestPad(config,std::filesystem::u8path(pad_file),
                [this] {
                    // Use the normal close path, which stops the guest before
                    // teardown. Quitting the UI loop alone leaves it running.
                    app_context().CallInUIThreadDeferred([this] {
                        if (auto* target=window()) {
                            rex::ui::UIEvent event(target);
                            if (static_cast<rex::ui::WindowListener*>(this)->OnCloseRequested(event))
                                target->RequestClose();
                        }
                    });
                });
        }
        // This game's UI uses texture descriptors tagged kInvalidTexture.
        // Keep the existing GPU compatibility path local to this title.
        if (!rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants","true"))
            throw std::runtime_error("Cannot enable SDOJ texture compatibility");
        REXLOG_INFO("SDOJ graphics compatibility: allow_invalid_fetch_constants=true");
    }
    // Normal launches use the shared INI and the game's own service namespace.
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {
        return std::make_shared<sdoj::SdojCommunity>(run().settings.endpoint,run().settings.nickname,run().source_build);
    }
    std::string NativeNickname(std::string name) override {return cave::community::NativeLegacyName(name);}
    std::shared_ptr<sdoj::SdojCommunity> test_community_;
};
REX_DEFINE_APP(sdoj, SdojApp::Create)
