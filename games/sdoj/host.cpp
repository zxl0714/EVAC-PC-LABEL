#include "sdoj_init.h"
#include "build_metadata.h"
#include "host.h"
#include <rex/cvar.h>
#include <stdexcept>

class SdojApp final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<SdojApp>(new SdojApp(context,"sdoj",PPCImageConfig,sdoj::kBuildIdentity));
    }
private:
    void OnGamePreSetup(rex::RuntimeConfig&) override {
        // This game's UI uses texture descriptors tagged kInvalidTexture.
        // Keep the existing GPU compatibility path local to this title.
        if (!rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants","true"))
            throw std::runtime_error("Cannot enable SDOJ texture compatibility");
        REXLOG_INFO("SDOJ graphics compatibility: allow_invalid_fetch_constants=true");
    }
    // Capability metadata keeps the profile offline until this title's native
    // community protocol has been adapted. Never borrow another game's boards.
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {return {};}
    std::string NativeNickname(std::string name) override {return name;}
};
REX_DEFINE_APP(sdoj, SdojApp::Create)
