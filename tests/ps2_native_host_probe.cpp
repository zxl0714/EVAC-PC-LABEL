// Authored integration workload: no ROM, BIOS, generated game code or save data.
#include "native_host.h"
#include "ps2_runtime.h"
#include "runtime/gs/gs_types.h"
#include "crt_mode.h"
#include <SDL3/SDL.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>

namespace {
unsigned drawn=0;bool fault=false,hold=false;
bool checkMenu{};
std::atomic_uint guestFrame{}, advances{}, rewinds{};
void Draw(uint8_t*,R5900Context* ctx,PS2Runtime* runtime) {
    if(fault && drawn==3)throw std::runtime_error("authored EE failure");
    auto& memory=runtime->memory();auto& gs=runtime->gs();
    if(!drawn) {
        memory.write64(0x12000010,cave::ps2::CrtSmode1(2));
        memory.write64(0x12000020,0);
        const auto display=cave::ps2::MakeSdkDisplay(2,false,false,0,640,224);
        memory.write64(0x12000000,1|(1u<<5)|(255u<<8));
        memory.write64(0x12000070,display.dispfb);
        memory.write64(0x12000080,display.display);
        gs.writeRegister(GS_REG_PRMODECONT,1);gs.writeRegister(GS_REG_XYOFFSET_1,0);
        gs.writeRegister(GS_REG_FRAME_1,uint64_t(10)<<16);
        gs.writeRegister(GS_REG_ZBUF_1,uint64_t(1)<<32);gs.writeRegister(GS_REG_TEST_1,0x30000);
        gs.writeRegister(GS_REG_SCISSOR_1,(uint64_t(639)<<16)|(uint64_t(223)<<48));
        gs.writeRegister(GS_REG_COLCLAMP,1);gs.writeRegister(GS_REG_PRIM,6|0x100);
    }
    gs.writeRegister(GS_REG_RGBAQ,0x3f80000080604020ull);
    gs.writeRegister(GS_REG_XYZ2,0);gs.writeRegister(GS_REG_XYZ2,uint64_t(640*16)|(uint64_t(224*16)<<16));
    gs.queueVBlankFrame();++drawn;
    if (checkMenu) {
        auto tick=memory.read32(0x1000)+1;
        memory.write32(0x1000,tick);
        if (guestFrame.exchange(tick)>tick) ++rewinds;
        ++advances;
    }
    if(hold)std::this_thread::sleep_for(std::chrono::milliseconds(16));
    ctx->pc=ctx->pc==0x100000?0x100004:0x100000;
}
struct MenuPad {
    SDL_JoystickID id{};
    SDL_Joystick* pad{};
    MenuPad() {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) throw std::runtime_error("Cannot initialize virtual controller");
        SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        desc.name="PS2 state menu contract";
        id=SDL_AttachVirtualJoystick(&desc);
        pad=id?SDL_OpenJoystick(id):nullptr;
        if (!pad || !SDL_IsGamepad(id)) throw std::runtime_error("Cannot create virtual menu controller");
    }
    ~MenuPad() { SDL_CloseJoystick(pad); SDL_DetachVirtualJoystick(id); SDL_QuitSubSystem(SDL_INIT_GAMEPAD); }
    void WriteShortcut(const std::filesystem::path& config) const {
        char guid[33]{};
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),guid,sizeof(guid));
        std::ofstream out(config);
        out.exceptions(std::ios::failbit|std::ios::badbit);
        out<<"[ps2_state_menu_controller]\n"<<guid<<"=b"<<int(SDL_GAMEPAD_BUTTON_BACK)<<",b"<<int(SDL_GAMEPAD_BUTTON_START)<<"\n";
    }
};
void CheckMenu(PS2Runtime& runtime,const std::filesystem::path& directory,bool crossConfirm,SDL_Joystick* pad) {
    using namespace std::chrono_literals;
    const auto wait=[](auto predicate,auto limit,const char* message) {
        const auto end=std::chrono::steady_clock::now()+limit;
        while (!predicate()) {
            if (std::chrono::steady_clock::now()>end) throw std::runtime_error(message);
            std::this_thread::sleep_for(15ms);
        }
    };
    const auto press=[&](std::initializer_list<SDL_GamepadButton> buttons) {
        for (auto button:buttons) SDL_SetJoystickVirtualButton(pad,button,true);
        std::this_thread::sleep_for(100ms);
        for (auto button:buttons) SDL_SetJoystickVirtualButton(pad,button,false);
        std::this_thread::sleep_for(100ms);
    };
    const auto open=[&] {
        if (crossConfirm) press({SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START});
        else {
            const auto game=GetForegroundWindow();
            DWORD pid{}; GetWindowThreadProcessId(game,&pid);
            if (pid!=GetCurrentProcessId() || !PostMessageW(game,WM_KEYDOWN,VK_F5,0) || !PostMessageW(game,WM_KEYUP,VK_F5,0))
                throw std::runtime_error("Cannot deliver F5 to test window");
        }
        std::this_thread::sleep_for(400ms);
        const auto old=advances.load();
        std::this_thread::sleep_for(200ms);
        if (advances!=old) throw std::runtime_error("Guest continued after menu shortcut");
    };
    const auto confirm=crossConfirm?SDL_GAMEPAD_BUTTON_SOUTH:SDL_GAMEPAD_BUTTON_EAST;
    const auto back=crossConfirm?SDL_GAMEPAD_BUTTON_EAST:SDL_GAMEPAD_BUTTON_SOUTH;
    wait([]{return advances>75;},8s,"Authored game did not start");
    wait([] { DWORD pid{}; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        return pid==GetCurrentProcessId(); },30s,"PS2 test window did not have input focus");
    std::this_thread::sleep_for(300ms);
    press({SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER});
    const auto afterShoulders=advances.load();
    wait([&]{return advances>afterShoulders+15;},3s,"Unbound shoulder buttons paused the guest");
    open();
    const auto file=directory/"slot-99.state";
    const auto frozen=guestFrame.load();
    press({SDL_GAMEPAD_BUTTON_DPAD_LEFT}); // 01 wraps back to 99.
    press({SDL_GAMEPAD_BUTTON_DPAD_DOWN});
    press({confirm}); // QUICK LOAD on NO DATA.
    if (guestFrame!=frozen || std::filesystem::exists(directory))
        throw std::runtime_error("Loading an empty slot changed the game or created a save");
    press({SDL_GAMEPAD_BUTTON_DPAD_UP});
    SDL_SetJoystickVirtualButton(pad,confirm,true);
    wait([&]{return std::filesystem::exists(file) && guestFrame>frozen+15;},8s,"QUICK SAVE did not resume play");
    SDL_SetJoystickVirtualButton(pad,confirm,false);
    std::this_thread::sleep_for(100ms);
    const auto firstWrite=std::filesystem::last_write_time(file);
    open();
    const auto reopened=guestFrame.load();
    press({SDL_GAMEPAD_BUTTON_DPAD_DOWN});
    press({confirm}); // Load is still 01 even though Save is 99.
    if (guestFrame!=reopened || std::filesystem::exists(directory/"slot-01.state"))
        throw std::runtime_error("QUICK LOAD followed the save slot");
    SDL_SetJoystickVirtualButton(pad,back,true);
    std::this_thread::sleep_for(200ms);
    if (guestFrame!=reopened) throw std::runtime_error("Menu Back leaked into the running game");
    SDL_SetJoystickVirtualButton(pad,back,false);
    wait([&]{return guestFrame>reopened+15;},5s,"Back did not resume play");
    open(); // Reopens on Load 01; switch to Save 99.
    const auto saved=guestFrame.load();
    press({SDL_GAMEPAD_BUTTON_DPAD_UP});
    press({confirm});
    press({confirm}); // Cancel is initially selected.
    if (std::filesystem::last_write_time(file)!=firstWrite)
        throw std::runtime_error("Cancel replaced an existing save");
    press({confirm});
    press({SDL_GAMEPAD_BUTTON_DPAD_RIGHT});
    press({confirm});
    wait([&]{return std::filesystem::last_write_time(file)!=firstWrite;},5s,"Replace did not overwrite slot 99");
    wait([&]{return guestFrame>saved+15;},5s,"Overwriting a save did not resume play");
    open();
    const auto beforeLoad=advances.load();
    press({SDL_GAMEPAD_BUTTON_DPAD_DOWN});
    press({SDL_GAMEPAD_BUTTON_DPAD_LEFT}); // Independently move Load from 01 to 99.
    press({confirm});
    std::this_thread::sleep_for(2000ms);
    if (advances!=beforeLoad) throw std::runtime_error("Game advanced during countdown");
    wait([]{return rewinds.load()!=0;},5s,"No game-state rewind following countdown");
    if (guestFrame<saved || guestFrame>saved+6) throw std::runtime_error("Loaded the wrong guest frame");
    std::cout<<"State menu: slot 99 saved, cancel/replace verified, restored at frame "<<saved<<", bytes="
             <<std::filesystem::file_size(file)<<'\n';
    runtime.requestStop();
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2){fault=std::string_view(argv[1])=="--fault";hold=std::string_view(argv[1])=="--hold";}
        auto runtime=std::make_unique<PS2Runtime>();
        cave::ps2::NativeHostOptions options{640,480};
        std::filesystem::path slots;
        std::optional<MenuPad> pad;
        const bool crossConfirm=argc==3 && std::string_view(argv[1])=="--state-menu-us";
        if (argc==3 && (crossConfirm || std::string_view(argv[1])=="--state-menu")) {
            hold=checkMenu=true;
            slots=std::filesystem::absolute(argv[2]);
            if (std::filesystem::exists(slots)) throw std::runtime_error("Menu probe needs a fresh output directory");
            options.stateIdentity="authored-test"; options.stateDirectory=slots;
            options.gameId=crossConfirm?"rtype-final":"espgaluda";
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
            pad.emplace();
            if (crossConfirm) {
                options.keyboardConfig=slots;
                options.keyboardConfig+=".ini";
                pad->WriteShortcut(options.keyboardConfig);
            }
        }
        runtime->nativeHost().Configure(std::move(options));
        unsigned presented=0;
        runtime->setDebugUiCallbacks([](PS2Runtime&,void*){},[](PS2Runtime& state,void* user){
            auto& presented=*static_cast<unsigned*>(user);
            if(++presented>=(hold?3600:10))state.requestStop();
        },[](PS2Runtime&,void*){},&presented);
        if(!runtime->initialize("EVAC PC LABEL | PS2 native host contract"))
            throw std::runtime_error("Native host initialization failed");
        runtime->registerFunction(0x100000,Draw);runtime->registerFunction(0x100004,Draw);
        runtime->cpu().pc=0x100000;
        std::exception_ptr menuError;
        std::jthread controller;
        if (checkMenu) controller=std::jthread([&] {
            try {CheckMenu(*runtime,slots,crossConfirm,pad->pad);}
            catch (...) {menuError=std::current_exception(); runtime->requestStop();}
        });
        try {runtime->run();}
        catch(const std::runtime_error& error) {
            if(fault && std::string_view(error.what())=="authored EE failure") {
                std::cout<<"Native host: EE failure propagated after queue/thread cleanup: PASS\n";return 0;
            }
            throw;
        }
        if (controller.joinable()) controller.join();
        if (menuError) std::rethrow_exception(menuError);
        if(fault || !presented || (!hold && presented!=10))throw std::runtime_error("Authored host frame count differs");
        std::cout<<"Native host: frames="<<presented<<" clean_shutdown=1: PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
