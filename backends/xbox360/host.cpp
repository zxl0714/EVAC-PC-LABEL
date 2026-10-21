#include "host.h"
#include "diagnostics/crash_reporter.h"
#include "input/configured_input.h"
#include "common/catalogue.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xex_module.h>
#include <rex/system/user_module.h>
#include <rex/system/xmemory.h>
#include <rex/system/xam/user_profile.h>
#include <rex/ui/keybinds.h>
#include <cstdio>
#include <mmsystem.h>
#include <stdexcept>

REXCVAR_DEFINE_STRING(project_root,"","Collection","Compatibility alias for collection_root")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(collection_root,"","Collection","Collection/package root")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(game_id,"","Collection","Selected game ID; must match the compiled program")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(launch_contract,"1","Collection","Collection launch contract version")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(host_timer_precision,true,"Host","Request a scoped 1 ms Windows timer period while running")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace cave::xbox360 {
namespace fs=std::filesystem;
namespace {
void Flag(const char* key,const std::string& value) {
    if (!rex::cvar::SetFlagByName(key,value)) throw std::runtime_error(std::string("Cannot apply runtime option: ")+key);
}
fs::path FindRoot() {
    auto requested=rex::cvar::GetFlagByName("collection_root");
    if (requested.empty()) requested=rex::cvar::GetFlagByName("project_root");
    if (!requested.empty()) return fs::canonical(cave::FromUtf8(requested));
    auto candidate=rex::filesystem::GetExecutableFolder();
    for (int level=0;level<5 && !candidate.empty();++level,candidate=candidate.parent_path())
        if (fs::is_regular_file(candidate/"user-data/config.ini") || fs::is_regular_file(candidate/"config.ini") ||
            fs::is_regular_file(candidate/"games/catalogue.json"))
            return fs::canonical(candidate);
    throw std::runtime_error("Cannot find collection root; specify --collection_root");
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
CollectionApp::CollectionApp(rex::ui::WindowedAppContext& context,std::string_view game,
                             rex::PPCImageInfo image,BuildIdentity build)
    :ReXApp(context,game,image),game_(game),build_(build) {
    if (build_.game!=game_) throw std::runtime_error("Compiled metadata belongs to another game");
}
CollectionApp::~CollectionApp() {
    if (timer_active_) timeEndPeriod(1);
}
bool CollectionApp::SetupEnvironment() {
    try {
        const auto selected=REXCVAR_GET(game_id);
        if ((!selected.empty() && selected!=game_) || REXCVAR_GET(launch_contract)!="1")
            throw std::runtime_error("Unsupported game ID or launch contract");
        const auto profile=rex::cvar::GetFlagByName("user_data_root");
        const auto data=rex::cvar::GetFlagByName("game_data_root");
        root_=FindRoot();
        run_=PrepareRun(root_,build_,profile.empty()?fs::path{}:cave::FromUtf8(profile),
                        data.empty()?fs::path{}:cave::FromUtf8(data));
        Flag("log_file",cave::Utf8(run_->directory/"game.log"));
        Flag("log_level",run_->settings.log_level);
        Flag("log_async_file","true");Flag("log_flush_interval","2");Flag("log_max_files","3");
        Flag("fullscreen",run_->settings.fullscreen?"true":"false");
        Flag("mnk_mode","false");Flag("allow_game_relative_writes","false");
        Flag("audio_master_volume",std::to_string(run_->settings.volume));
        const auto mappings=rex::filesystem::GetExecutableFolder()/"gamecontrollerdb.txt";
        Flag("hid_mappings_file",fs::is_regular_file(mappings)?cave::Utf8(mappings):"");
        return ReXApp::SetupEnvironment();
    } catch (const std::exception& error) {
        REXLOG_ERROR("Startup refused: {}",error.what());
        std::fprintf(stderr,"Startup refused: %s\n",error.what());
        if (run_) {try {RecordStartupError(*run_,error.what());} catch (...) {}}
        else if (!root_.empty()) {try {RecordEarlyStartupError(root_,game_,error.what());} catch (...) {}}
        return false;
    }
}
void CollectionApp::OnConfigurePaths(rex::PathConfig& paths) {
    paths.game_data_root=run_->paths.data;paths.user_data_root=run_->profile;
    paths.cache_root=run_->paths.cache;paths.metadata_root=run_->profile/"metadata";
    paths.update_data_root.clear();paths.config_path.clear(); // Never read/save a second TOML.
}
void CollectionApp::OnPostInitLogging() {
    const bool requested=REXCVAR_GET(host_timer_precision);
    if (requested && !timer_active_) timer_active_=timeBeginPeriod(1)==TIMERR_NOERROR;
    REXLOG_INFO("HOST_TIMING timer_period_ms=1 requested={} active={} guest_clock_scale=unchanged",requested,timer_active_);
    if (!StartCrashReporter(run_->directory,game_,run_->source_build))
        throw std::runtime_error("Cannot start the crash writer; check runtime files");
    REXLOG_INFO("Collection diagnostics ready; game={} report={} configuration=root-INI community={}",
        game_,cave::Utf8(run_->directory),run_->settings.community_enabled?"configured":"disabled");
}
void CollectionApp::OnPreSetup(rex::RuntimeConfig& config) {
    config.gpu_plugin="xenos";config.graphics=rex::system::LoadGpuPlugin(config.gpu_plugin);
    if (!config.graphics) throw std::runtime_error("Cannot load the Xenos GPU plugin");
    Flag("vsync",run_->settings.vsync?"true":"false");
    const auto settings=run_->settings;
    config.input_factory=[settings](bool tools) {return CreateConfiguredInput(settings,tools);};
    OnGamePreSetup(config);
}
void CollectionApp::OnCreateDialogs(rex::ui::ImGuiDrawer*) {
    window()->SetTitle("CAVE PC Collection — "+std::string(cave::FindGame(game_).DisplayName()));
    rex::ui::UnregisterBind("bind_settings");shortcuts_=std::make_unique<Shortcuts>(window());
}
void CollectionApp::OnPostLoadXexImage() {
    auto module=runtime()->kernel_state()->GetExecutableModule();
    CrashModule("default.xex","main-loaded",reinterpret_cast<uint64_t>(runtime()->memory()->virtual_membase()),
                module->xex_module()->base_address(),module->xex_module()->image_size());
}
void CollectionApp::OnPostSetup() {
    if (run_->settings.community_enabled) {
        community_=CreateCommunity();
        if (!community_) throw std::runtime_error("Game community provider is unavailable");
        runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(run_->settings.nickname,true,NativeNickname(run_->settings.nickname));
        rex::system::xam::SetCommunityService(community_);
    } else if (!cave::FindGame(game_).community_supported) {
        runtime()->kernel_state()->user_profile()->ConfigureCommunityProfile(
            run_->settings.nickname,false,NativeNickname(run_->settings.nickname));
        REXLOG_INFO("Community unavailable for game={}; local profile only, no network worker",game_);
    }
    game_started_=true;OnGameSetup();
    REXLOG_INFO("Host initialized; game={} full-screen={} vsync={} volume={} keyboard_slot=0 gamepad={}",
        game_,run_->settings.fullscreen,run_->settings.vsync,run_->settings.volume,run_->settings.gamepad_enabled);
}
void CollectionApp::StopGame() {
    if (community_) {
        if (runtime() && runtime()->kernel_state()) rex::system::xam::CancelCommunityIo(runtime()->kernel_state());
        rex::system::xam::SetCommunityService(nullptr);community_->Stop();community_.reset();
    }
    if (game_started_) {game_started_=false;OnGameStop();}
    if (timer_active_) {timeEndPeriod(1);timer_active_=false;}
}
void CollectionApp::OnShutdown() {StopGame();shortcuts_.reset();}
bool CollectionApp::OnWindowCloseRequested() {
    REXLOG_INFO("User requested close");StopGame();rex::FlushLogging();return true;
}
}
