#include "state_menu.h"
#include "native_host.h"
#include "gs_display.h"
#include "state_slots.h"
#include "machine_state.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "state_menu_input.h"
#include <chrono>
#include <ctime>
#include <future>
#include <iostream>

namespace cave::ps2 {
namespace {
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
enum class Page { Closed, Opening, Menu, Overwrite, Saving, Loading, Countdown, Error };
std::wstring Digits(unsigned n) { return (n<10?L"0":L"")+std::to_wstring(n); }

std::wstring SlotDetails(const std::optional<std::filesystem::file_time_type>& fileTime) {
    if (!fileTime) return L"NO DATA";
    const auto time=std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(*fileTime));
    std::tm local{};
    wchar_t stamp[32]{};
    if (localtime_s(&local,&time) || !std::wcsftime(stamp,std::size(stamp),L"%Y-%m-%d  %H:%M:%S",&local))
        return L"Saved";
    return stamp;
}

// Draw only when the selection changes. Windows' font rasterizer also works
// on SteamOS through Wine and needs no additional font files in the package.
class Panel {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ original{};
    uint32_t* pixels{};
public:
    static constexpr int Width=624, Height=504;
    Panel() {
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=Width; info.bmiHeader.biHeight=-Height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        dc=CreateCompatibleDC(nullptr);
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if (!dc || !bitmap) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            throw std::runtime_error("Cannot create PS2 save state menu surface");
        }
        original=SelectObject(dc,bitmap);
        SetBkMode(dc,TRANSPARENT);
        Box(0,0,Width,Height,RGB(20,29,31));
    }
    ~Panel() { SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
    void Box(int x,int y,int w,int h,COLORREF color) {
        RECT area{x,y,x+w,y+h};
        SetDCBrushColor(dc,color);
        FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    void Text(std::wstring_view value,int x,int y,int height,COLORREF color,bool bold=false) {
        const auto font=CreateFontW(-height,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto old=SelectObject(dc,font);
        SetTextColor(dc,color); TextOutW(dc,x,y,value.data(),int(value.size()));
        SelectObject(dc,old); DeleteObject(font);
    }
    void Upload(GsDisplay& display,bool countdown=false) {
        GdiFlush();
        for (int y=0; y<Height; ++y) for (int x=0; x<Width; ++x) {
            auto& p=pixels[y*Width+x];
            const bool visible=countdown ? ((x-312)*(x-312)+(y-252)*(y-252)<=52*52) : (y>=86 && y<416);
            p=visible?(p|0xff000000u):0;
        }
        display.SetMenuPanel(Width,Height,{reinterpret_cast<uint8_t*>(pixels),Width*Height*4});
    }
};
}

struct StateMenu::Impl {
    NativeHost& host;
    GsDisplay& display;
    PS2Runtime& runtime;
    StateSlots slots;
    StateMenuInput input;
    decltype(slots.List()) entries{};
    Page page=Page::Closed;
    bool dirty{}, replace{}, loaded{}, circleConfirm;
    StateSlots::Selection selection, remembered;
    unsigned digit{}, dismissButtons{};
    Clock::time_point nextDraw{}, resumeAt{};
    std::future<void> job;
    std::string error;
    std::string previousError;
    Impl(NativeHost& host,GsDisplay& display,PS2Runtime& runtime,std::filesystem::path directory,
         std::string identity,const std::filesystem::path& config,bool usePad,bool keyEvents,
         HostPadSource& pad,bool confirmWithCircle)
        :host(host),display(display),runtime(runtime),slots(std::move(directory),std::move(identity)),
         input(config,usePad,keyEvents,pad,confirmWithCircle),circleConfirm(confirmWithCircle),
         selection(slots.LastUsed()),remembered(selection) {}

    void RememberSelection() {
        if (selection==remembered) return;
        try { slots.Remember(selection); remembered=selection; }
        catch (const std::exception& e) { std::cerr<<"[ps2-state] menu selection: "<<e.what()<<'\n'; }
    }

    bool CanNavigate() const { return page==Page::Menu || page==Page::Overwrite || page==Page::Error; }
    void Show(Page value) { page=value; dirty=true; }
    void Open() {
        loaded=false;
        runtime.eeScheduler().setPaused(true);
        // Audio and picture are frozen at the very same EE dispatch boundary.
        // The owner continues consuming graphics commands while this job runs.
        job=runtime.eeScheduler().whilePaused([this] {
            MachineState::PauseAudio(runtime,true);
            host.CapturePausedPicture();
        });
        Show(Page::Opening);
    }
    void Resume() {
        RememberSelection();
        MachineState::PauseAudio(runtime,false,loaded);
        runtime.eeScheduler().setPaused(false);
        display.CloseMenu();
        dismissButtons=0;
        Show(Page::Closed);
    }
    void Dismiss(unsigned buttons) {
        // The menu and guest share a pad. Keep the guest parked until the
        // button that closed the overlay is up, so it cannot also back out.
        if (input.Held(buttons)) dismissButtons=buttons;
        else Resume();
    }
    void Start(bool save) {
        const auto selected=selection.CurrentSlot();
        RememberSelection();
        job=runtime.eeScheduler().whilePaused([this,selected,save] {
            if (save) slots.Save(selected,host.SavePausedState(runtime));
            else host.LoadSlot(runtime,slots,selected);
        });
        Show(save?Page::Saving:Page::Loading);
    }
    void CompleteJob() {
        if (!job.valid() || job.wait_for(0ms)!=std::future_status::ready) return;
        job.get();
        if (page==Page::Opening) {
            entries=slots.List();
            Show(Page::Menu); return;
        }
        loaded=page==Page::Loading;
        if (!previousError.empty()) {
            error=std::exchange(previousError,{});
            Show(Page::Error);
            return;
        }
        if (loaded) { resumeAt=Clock::now()+3s; digit=3; Show(Page::Countdown); }
        else Resume();
    }
    void Handle(unsigned key) {
        if (dismissButtons) { Dismiss(dismissButtons); return; }
        if (page==Page::Closed) { if (key&Toggle) Open(); return; }
        if (page==Page::Opening || page==Page::Loading || page==Page::Saving || page==Page::Countdown) return;
        if (key&Toggle) { Dismiss(Toggle); return; }
        if (page==Page::Error) { if (key&(Back|Accept)) Dismiss(key&(Back|Accept)); return; }
        if (key&Back) {
            if (page==Page::Overwrite) Show(Page::Menu);
            else Dismiss(Back);
            return;
        }
        if (page==Page::Overwrite) {
            if (key&(Left|Right)) { replace=!replace; dirty=true; }
            else if (key&Accept) {
                if (replace) Start(true);
                else Show(Page::Menu);
            }
            return;
        }
        auto& slot=selection.CurrentSlot();
        if (key&(Up|Down)) {
            selection.load=!selection.load;
        } else if (key&Left) {
            slot=slot==1?StateSlots::Count:slot-1;
        } else if (key&Right) {
            slot=slot==StateSlots::Count?1:slot+1;
        } else if (key&Accept) {
            if (selection.load) {
                if (entries[slot-1]) Start(false);
            } else if (entries[slot-1]) {
                replace=false;
                Show(Page::Overwrite);
            } else Start(true);
            return;
        } else return;
        dirty=true;
    }
    void DrawRow(Panel& p,bool isLoad,int y) const {
        const bool selected=isLoad==selection.load;
        const unsigned slot=isLoad ? selection.loadSlot : selection.saveSlot;
        constexpr auto white=RGB(246,246,239), muted=RGB(174,190,185), gold=RGB(246,189,98);
        if (selected) {
            p.Box(24,y,576,76,RGB(53,77,72));
            p.Box(24,y,4,76,gold);
        }
        p.Text(isLoad?L"QUICK LOAD":L"QUICK SAVE",44,y+10,25,selected?white:muted,true);
        p.Text(L"‹    "+Digits(slot)+L"    ›",479,y+9,26,selected?gold:muted,true);
        if (selected) p.Text(SlotDetails(entries[slot-1]),44,y+45,19,muted);
    }
    void Paint() {
        Panel p;
        constexpr auto white=RGB(246,246,239), muted=RGB(174,190,185), gold=RGB(246,189,98);
        if (page==Page::Countdown) {
            p.Text(std::to_wstring(digit),290,203,76,white,true);
            p.Upload(display,true); return;
        }
        p.Box(0,86,Panel::Width,4,gold);
        p.Text(L"Save states",32,106,33,white,true);
        const std::wstring_view buttons=circleConfirm?L"○  Confirm     ×  Back":L"×  Confirm     ○  Back";
        if (page==Page::Menu) {
            DrawRow(p,false,172);
            DrawRow(p,true,selection.load?230:256);
            p.Text(L"↑ ↓  Action      ← →  Slot",32,365,19,muted);
            p.Text(buttons,352,365,19,muted);
        } else if (page==Page::Overwrite) {
            const auto slot=selection.saveSlot;
            p.Text(L"Replace the save in slot "+Digits(slot)+L"?",32,172,27,white);
            p.Text(SlotDetails(entries[slot-1]),32,215,20,muted);
            p.Box(replace?312:32,273,264,49,RGB(53,77,72));
            p.Text(L"Cancel",52,282,23,replace?muted:gold,!replace);
            p.Text(L"Replace",332,282,23,replace?gold:muted,replace);
            p.Text(L"← →  Choose",32,365,19,muted);
            p.Text(buttons,352,365,19,muted);
        } else if (page==Page::Error) {
            p.Text(L"Could not complete the operation",32,172,28,white);
            p.Text(FromUtf8(error.substr(0,53)).wstring(),32,216,18,muted);
            p.Text(L"Press confirm to return to the game",32,365,20,white);
        } else p.Text(page==Page::Saving?L"Saving…":L"Loading…",32,200,28,white);
        p.Upload(display);
    }
    void Update() {
        try {
            CompleteJob();
            Handle(input.Poll(display.Focused(),CanNavigate()));
        } catch (const std::exception& e) {
            error=e.what();
            std::cerr<<"[ps2-state] "<<error<<'\n';
            Show(Page::Error);
        }
        if (page==Page::Closed || page==Page::Opening) return;
        const auto now=Clock::now();
        if (page==Page::Countdown) {
            if (now>=resumeAt) { Resume(); return; }
            const unsigned next=unsigned(std::chrono::ceil<std::chrono::seconds>(resumeAt-now).count());
            if (next!=digit) { digit=next; dirty=true; }
        }
        if (dirty || now>=nextDraw) {
            if (dirty) Paint();
            display.PresentMenu();
            dirty=false; nextDraw=now+100ms;
        }
    }
};

StateMenu::StateMenu(NativeHost& host,GsDisplay& display,PS2Runtime& runtime,std::filesystem::path directory,
                     std::string identity,const std::filesystem::path& config,bool gamepad,bool keyEvents,
                     HostPadSource& pad,bool circleConfirm)
    :impl(std::make_unique<Impl>(host,display,runtime,std::move(directory),std::move(identity),config,gamepad,keyEvents,pad,circleConfirm)) {}
StateMenu::~StateMenu() { impl->RememberSelection(); }
void StateMenu::RestoreOnEntry(unsigned slot,std::string previousError,std::function<void()> restore) {
    impl->selection.loadSlot=slot;
    impl->selection.load=true;
    impl->previousError=std::move(previousError);
    impl->job=impl->runtime.eeScheduler().whilePaused([this,restore=std::move(restore)] {
        MachineState::PauseAudio(impl->runtime,true);
        restore();
    });
    impl->Show(Page::Loading);
}
void StateMenu::Update() { impl->Update(); }
void StateMenu::Key(unsigned code,bool down,uint16_t buttons) { impl->input.Key(code,down,buttons,impl->CanNavigate()); }
}
