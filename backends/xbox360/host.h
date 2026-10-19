#pragma once
#include "diagnostics/run_context.h"
#include <rex/rex_app.h>
#include <rex/system/xam/community_service.h>
#include <optional>

namespace cave::xbox360 {
class CollectionApp : public rex::ReXApp {
public:
    CollectionApp(rex::ui::WindowedAppContext& context,std::string_view game,
                  rex::PPCImageInfo image,BuildIdentity build);
    ~CollectionApp() override;
protected:
    const RunContext& run() const {return *run_;}
    virtual std::shared_ptr<rex::system::xam::CommunityService> CreateCommunity()=0;
    virtual std::string NativeNickname(std::string nickname)=0;
    virtual void OnGamePreSetup(rex::RuntimeConfig&) {}
    virtual void OnGameSetup() {}
    virtual void OnGameStop() {}
private:
    bool SetupEnvironment() final;
    void OnConfigurePaths(rex::PathConfig& paths) final;
    void OnPostInitLogging() final;
    void OnPreSetup(rex::RuntimeConfig& config) final;
    void OnCreateDialogs(rex::ui::ImGuiDrawer*) final;
    void OnPostLoadXexImage() final;
    void OnPostSetup() final;
    void OnShutdown() final;
    bool OnWindowCloseRequested() final;
    void StopGame();
    const std::string game_;
    BuildIdentity build_;
    std::optional<RunContext> run_;
    std::filesystem::path root_;
    std::unique_ptr<rex::ui::WindowInputListener> shortcuts_;
    std::shared_ptr<rex::system::xam::CommunityService> community_;
    bool timer_active_{},game_started_{};
};
}
