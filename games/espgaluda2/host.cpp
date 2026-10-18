#include "espgaluda2_init.h"
#include "build_metadata.h"
#include "host.h"
#include "community/g2_service.h"

class Espgaluda2App final : public cave::xbox360::CollectionApp {
public:
    using CollectionApp::CollectionApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<Espgaluda2App>(new Espgaluda2App(context,"espgaluda2",PPCImageConfig,espgaluda2::kBuildIdentity));
    }
private:
    std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity() override {
        REXLOG_INFO("G2 community bridge installed; game=espgaluda2 protocol=1 replay=espgaluda2-native-v2");
        return std::make_shared<espgaluda2::G2Community>(run().settings.endpoint,run().settings.nickname,run().source_build);
    }
    std::string NativeNickname(std::string name) override {return espgaluda2::CommunityNativeName(name);}
};
REX_DEFINE_APP(espgaluda2, Espgaluda2App::Create)
