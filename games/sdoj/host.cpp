#include "sdoj_init.h"
#include "build_metadata.h"
#include "host.h"

class SdojApp final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<SdojApp>(new SdojApp(context,"sdoj",PPCImageConfig,sdoj::kBuildIdentity));
    }
private:
    // Capability metadata keeps the profile offline until this title's native
    // community protocol has been adapted. Never borrow another game's boards.
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {return {};}
    std::string NativeNickname(std::string name) override {return name;}
};
REX_DEFINE_APP(sdoj, SdojApp::Create)
