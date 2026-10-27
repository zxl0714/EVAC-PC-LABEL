#include "guwange_init.h"
#include "build_metadata.h"
#include "host.h"
#include "common/nickname.h"
#include "community/guwange_service.h"
#include "performance/native_wait.h"
#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

REXCVAR_DEFINE_STRING(guwange_community_test_endpoint,"","Diagnostics","Explicit loopback-only Guwange integration test provider")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

class GuwangeApp final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<GuwangeApp>(new GuwangeApp(
            context,"guwange",PPCImageConfig,guwange::kBuildIdentity));
    }
private:
    void OnGameSetup() override {
        const auto endpoint=rex::cvar::GetFlagByName("guwange_community_test_endpoint");
        if(endpoint.empty()) return;
        if(run().settings.community_enabled || !endpoint.starts_with("http://127.0.0.1:"))
            throw std::runtime_error("Guwange development provider requires disabled regular community and an explicit loopback endpoint");
        test_community_=std::make_shared<guwange::GuwangeCommunity>(
            endpoint,run().settings.nickname,run().source_build);
        runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(
            run().settings.nickname,true,NativeNickname(run().settings.nickname));
        rex::system::xam::SetCommunityService(test_community_);
        REXLOG_WARN("Guwange development community provider enabled; loopback only, capability acceptance remains pending");
    }
    void OnGameStop() override {
        guwange::performance::StopNativeWait();
        if(!test_community_) return;
        if(runtime() && runtime()->kernel_state())
            rex::system::xam::CancelCommunityIo(runtime()->kernel_state());
        rex::system::xam::SetCommunityService({});
        test_community_->Stop();test_community_.reset();
    }
    void OnGamePreSetup(rex::RuntimeConfig&) override {
        guwange::performance::StartNativeWait();
        // The base game's UI emits type-0 texture fetch constants. Reuse the
        // existing Xenos compatibility path, scoped to this executable.
        if (!rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true"))
            throw std::runtime_error("Cannot enable Guwange texture compatibility");
        // The extracted XBLA program asks XAM for its edition. The shared SDK
        // defaults to the trial; this standalone title runs the full edition.
        // Keep its native mode/save gates, and do not change other titles.
        if (!rex::cvar::SetFlagByName("license_mask", "1"))
            throw std::runtime_error("Cannot configure Guwange XBLA edition");
        REXLOG_INFO("Guwange graphics compatibility: allow_invalid_fetch_constants=true");
        REXLOG_INFO("Guwange XBLA edition: license_mask=1");
    }
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {
        REXLOG_INFO("Guwange community bridge installed; game=guwange protocol=1 replay=guwange-base-native-v1");
        return std::make_shared<guwange::GuwangeCommunity>(
            run().settings.endpoint,run().settings.nickname,run().source_build);
    }
    std::string NativeNickname(std::string name) override {
        return cave::LegacyDisplayNickname(cave::NormalizeDisplayNickname(name));
    }
    std::shared_ptr<guwange::GuwangeCommunity> test_community_;
};
REX_DEFINE_APP(guwange, GuwangeApp::Create)
