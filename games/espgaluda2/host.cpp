#include "espgaluda2_init.h"
#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xex_module.h>
#include <rex/system/user_module.h>
#include <rex/system/xmemory.h>
#include <rex/system/xam/user_profile.h>
#include <rex/ui/keybinds.h>
#include "diagnostics/run_context.h"
#include "diagnostics/crash_reporter.h"
#include "input/configured_input.h"
#include "community/g2_service.h"
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <mmsystem.h>

REXCVAR_DEFINE_STRING(project_root,"","ESPGaluda2","Project/package root; autodetected beside runtime when empty")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(host_timer_precision,true,"Host","Request a scoped 1 ms Windows timer period while running")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
namespace fs=std::filesystem;
class TimerPeriod final {
public:
    ~TimerPeriod() {Stop();}
    void Start(bool enabled) {
        if (enabled && !active_) active_=timeBeginPeriod(1)==TIMERR_NOERROR;
    }
    void Stop() {if (active_) {timeEndPeriod(1); active_=false;}}
    bool active() const {return active_;}
private:
    bool active_=false;
};
std::string Utf8(const fs::path& path) {
    auto text=path.u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
fs::path FromUtf8(const std::string& text) {return fs::u8path(text);}
void Flag(const char* key,const std::string& value) {
    if (!rex::cvar::SetFlagByName(key,value)) throw std::runtime_error(std::string("Cannot apply runtime option: ")+key);
}
fs::path FindRoot() {
    auto requested=rex::cvar::GetFlagByName("project_root");
    if (!requested.empty()) return fs::canonical(FromUtf8(requested));
    auto candidate=rex::filesystem::GetExecutableFolder();
    for (int level=0;level<5 && !candidate.empty();++level,candidate=candidate.parent_path())
        if (fs::is_regular_file(candidate/"game-data/default.xex")) return fs::canonical(candidate);
    throw std::runtime_error("Cannot find game-data/default.xex; use the root launch script");
}
class Shortcuts final : public rex::ui::WindowInputListener {
public:
    explicit Shortcuts(rex::ui::Window* window):window_(window) {window_->AddInputListener(this,128);}
    ~Shortcuts() override {window_->RemoveInputListener(this);}
    void OnKeyDown(rex::ui::KeyEvent& event) override {
        if (event.is_handled() || event.prev_state()) return;
        using Key=rex::ui::VirtualKey;
        if (event.virtual_key()==Key::kF11 || (event.virtual_key()==Key::kReturn && event.is_alt_pressed())) {
            window_->SetFullscreen(!window_->IsFullscreen());
            REXLOG_INFO("DISPLAY fullscreen={} persistence=this-run-only",window_->IsFullscreen());
            event.set_handled(true);
        } else if (event.virtual_key()==Key::kF8) {
            LARGE_INTEGER now{};QueryPerformanceCounter(&now);
            REXLOG_INFO("USER_MARKER key=F8 qpc={}",now.QuadPart);
            rex::FlushLogging();event.set_handled(true);
        }
    }
private:rex::ui::Window* window_;
};
}

class Espgaluda2App final : public rex::ReXApp {
public:
    using rex::ReXApp::ReXApp;
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
        return std::unique_ptr<Espgaluda2App>(new Espgaluda2App(context, "espgaluda2", PPCImageConfig));
    }
protected:
    bool SetupEnvironment() override {
        try {
            const auto profile=rex::cvar::GetFlagByName("user_data_root");
            root_=FindRoot();
            run_=espgaluda2::PrepareRun(root_,profile.empty()?fs::path{}:FromUtf8(profile));
            Flag("log_file",Utf8(run_->directory/"game.log"));
            Flag("log_level",run_->settings.log_level);
            Flag("log_async_file","true");
            Flag("log_flush_interval","2");
            Flag("log_max_files","3");
            Flag("fullscreen",run_->settings.fullscreen?"true":"false");
            Flag("mnk_mode","false"); // A keyboard-only driver below never captures the mouse.
            Flag("allow_game_relative_writes","false");
            Flag("audio_master_volume",std::to_string(run_->settings.volume));
            const auto mappings=rex::filesystem::GetExecutableFolder()/"gamecontrollerdb.txt";
            Flag("hid_mappings_file",fs::is_regular_file(mappings)?Utf8(mappings):"");
            return ReXApp::SetupEnvironment();
        } catch (const std::exception& error) {
            REXLOG_ERROR("Startup refused: {}",error.what());
            std::fprintf(stderr,"Startup refused: %s\n",error.what());
            if (run_) {try {espgaluda2::RecordStartupError(*run_,error.what());} catch (...) {}}
            else if (!root_.empty()) {try {espgaluda2::RecordEarlyStartupError(root_,error.what());} catch (...) {}}
            return false;
        }
    }
    void OnConfigurePaths(rex::PathConfig& paths) override {
        paths.game_data_root=run_->root/"game-data";
        paths.user_data_root=run_->profile;
        paths.cache_root=run_->profile/"cache";
        paths.metadata_root=run_->profile/"metadata";
        paths.update_data_root.clear();
        paths.config_path.clear(); // The base class must not read or write a second TOML config.
    }
    void OnPostInitLogging() override {
        const bool requested=REXCVAR_GET(host_timer_precision);
        timer_period_.Start(requested);
        REXLOG_INFO("HOST_TIMING timer_period_ms=1 requested={} active={} guest_clock_scale=unchanged",
                    requested,timer_period_.active());
        if (!espgaluda2::StartCrashReporter(run_->directory))
            throw std::runtime_error("Cannot start the crash writer; check runtime files");
        REXLOG_INFO("ESPGaluda2 diagnostics ready; report={} configuration=root-INI community={}",
            Utf8(run_->directory),run_->settings.community_enabled?"configured":"disabled");
    }
    void OnPreSetup(rex::RuntimeConfig& config) override {
        config.gpu_plugin="xenos";
        config.graphics=rex::system::LoadGpuPlugin(config.gpu_plugin);
        if (!config.graphics) throw std::runtime_error("Cannot load the Xenos GPU plugin");
        // Plugin cvars exist only after the DLL is loaded, before presentation.
        Flag("vsync",run_->settings.vsync?"true":"false");
        const auto settings=run_->settings;
        config.input_factory=[settings](bool toolMode) {return espgaluda2::CreateConfiguredInput(settings,toolMode);};
    }
    void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {
        rex::ui::UnregisterBind("bind_settings"); // No alternate player config / save button.
        shortcuts_=std::make_unique<Shortcuts>(window());
    }
    void OnPostLoadXexImage() override {
        auto module=runtime()->kernel_state()->GetExecutableModule();
        espgaluda2::CrashModule("default.xex","main-loaded",
            reinterpret_cast<uint64_t>(runtime()->memory()->virtual_membase()),
            module->xex_module()->base_address(),module->xex_module()->image_size());
    }
    void OnPostSetup() override {
        if (run_->settings.community_enabled) {
            community_=std::make_shared<espgaluda2::G2Community>(run_->settings.endpoint,run_->settings.nickname,run_->source_build);
            runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(run_->settings.nickname,true,
                espgaluda2::CommunityNativeName(run_->settings.nickname));
            rex::system::xam::SetCommunityService(community_);
            REXLOG_INFO("G2 community bridge installed; game=espgaluda2 protocol=1 replay=espgaluda2-native-v2");
        }
        REXLOG_INFO("Host initialized; full-screen={} vsync={} volume={} keyboard_slot=0 gamepad={}",
            run_->settings.fullscreen,run_->settings.vsync,run_->settings.volume,run_->settings.gamepad_enabled);
    }
    void OnShutdown() override {StopCommunity();shortcuts_.reset(); timer_period_.Stop();}
    bool OnWindowCloseRequested() override {
        REXLOG_INFO("User requested close");StopCommunity();timer_period_.Stop();rex::FlushLogging();return true;
    }
private:
    void StopCommunity() {
        if (!community_) return;
        if (runtime() && runtime()->kernel_state()) rex::system::xam::CancelCommunityIo(runtime()->kernel_state());
        rex::system::xam::SetCommunityService(nullptr);
        community_->Stop();community_.reset();
    }
    std::optional<espgaluda2::RunContext> run_;
    fs::path root_;
    std::unique_ptr<Shortcuts> shortcuts_;
    TimerPeriod timer_period_;
    std::shared_ptr<espgaluda2::G2Community> community_;
};
REX_DEFINE_APP(espgaluda2, Espgaluda2App::Create)
