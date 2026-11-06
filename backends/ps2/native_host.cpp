#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_host.h"
#include "startup_trace.h"
#include "host_input.h"
#include "gs_display.h"
#include "gs_command_queue.h"
#include "gs_completion_mailbox.h"
#include "gs_device_sink.h"
#include "gs_trace.h"
#include "ready_scanout.h"
#include "../ps4/input/keyboard_config.h"
#include "../ps4/input/sdl_gamepad.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <Windows.h>
#include "../../common/windows_timer_resolution.h"
#include "common/settings.h"
#include "frame_trace.h"
#include <optional>
#include "input_trace.h"
#include "../../tools/latency/test_pad.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace cave::ps2 {
namespace {
using namespace std::chrono_literals;
uint16_t Ps2Buttons(uint32_t bits) {return uint16_t(bits)|((bits&0x100000)?1u:0u);}
class SharedQueue final : public GsCommandBackend {
    std::shared_ptr<GsCommandQueue> queue;
public:
    explicit SharedQueue(std::shared_ptr<GsCommandQueue> value):queue(std::move(value)){}
    void Initialize(std::span<const uint8_t> data) override{queue->Initialize(data);}
    void Reset() override{queue->Reset();}
    void Gif(GifPathId path,std::span<const uint8_t> data) override{queue->Gif(path,data);}
    void Register(uint8_t reg,uint64_t value) override{queue->Register(reg,value);}
    void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> data) override{queue->Image(setup,data);}
    size_t ReadFifo(std::span<uint8_t> data) override{return queue->ReadFifo(data);}
    void Snapshot(std::vector<uint8_t>& data) override{queue->Snapshot(data);}
    void WriteVram(size_t offset,std::span<const uint8_t> data) override{queue->WriteVram(offset,data);}
    void Frame(const GsFrame& frame) override{queue->Frame(frame);}
    bool TryFrame(const GsFrame& frame) override{return queue->TryFrame(frame);}
    GsCompletions CollectCompletions() override{return queue->CollectCompletions();}
    void AcknowledgeStatus(uint32_t mask) override{queue->AcknowledgeStatus(mask);}
    void AcknowledgeSignal() override{queue->AcknowledgeSignal();}
};
class Input final : public HostPadSource {
    kt::input::SdlGamepad gamepad;
    kt::input::KeyboardBindings bindings{};
    std::array<bool,256> keys{};
    std::array<std::chrono::steady_clock::time_point,16> until{};
    uint16_t held=0;
    std::mutex mutex;
    bool focused=false;
public:
    explicit Input(const NativeHostOptions& options) {
        // The established process-local test pad is opt-in and requires both
        // fresh diagnostic traces. It never sends Windows/controller input.
        if(input_trace::Enabled() && evac_latency::Initialize())evac_latency::InitializeTestPad();
        if(options.gameId.empty())
            bindings=options.keyboardConfig.empty()?kt::input::DefaultKeyboardBindings():kt::input::LoadKeyboardConfig(options.keyboardConfig);
        else {
            const auto settings=LoadSettings(options.keyboardConfig,options.gameId);
            constexpr std::array<uint32_t,16> masks{0x10,0x40,0x80,0x20,0x4000,0x2000,0x8000,0x1000,8,0x100000,0x400,0x800,0x100,0x200,2,4};
            for(size_t i=0;i<masks.size();++i)for(const auto key:settings.keyboard[i].keys)bindings.at(key)|=masks[i];
        }
        gamepad.Configure(options.gamepadEnabled,options.deadzone);gamepad.Initialize();
    }
    void Key(unsigned key,bool down) {
        if(key>=keys.size())return;
        std::lock_guard lock(mutex);
        if(keys[key]==down)return;
        keys[key]=down;
        const auto mask=Ps2Buttons(bindings[key]);
        input_trace::Record(input_trace::Kind::HostKey,0,key,mask,down);
        if(down) {
            const auto duration=DiagnosticTapMilliseconds();
            if(duration && mask) {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration);
                for(unsigned i=0;i<16;++i)if(mask&(1u<<i))until[i]=deadline;
                std::cerr<<"[ps2-pad-event] key="<<key<<" active=0x"<<std::hex<<mask<<std::dec<<" pulse_ms="<<duration<<'\n';
            }
        }
        held=0;
        for(unsigned i=0;i<keys.size();++i)if(keys[i])held|=Ps2Buttons(bindings[i]);
    }
    void Focus(bool focus) {
        std::lock_guard lock(mutex);
        if(focused==focus)return;
        if(!focus){keys.fill(false);held=0;until.fill({});}
        focused=focus;
        gamepad.Focus(focus);
    }
    HostPadState Read(unsigned port) override {
        if(port)return {};
        HostPadState state;
        if(evac_latency::test_pad) {
            const auto sample=evac_latency::ReadTestPad();
            constexpr std::array<uint16_t,16> masks{
                0x10,0x40,0x80,0x20,8,1,2,4,0x400,0x800,0,0,0x4000,0x2000,0x8000,0x1000};
            for(unsigned i=0;i<masks.size();++i)if(sample.buttons&(1u<<i))state.buttons|=masks[i];
            return state;
        }
        std::lock_guard lock(mutex);
        if(!focused)return {};
        const auto current=gamepad.Poll();
        state.buttons=held|Ps2Buttons(current.buttons);
        state.axes={current.right_x,current.right_y,current.left_x,current.left_y};
        if(DiagnosticTapMilliseconds()){
            const auto now=std::chrono::steady_clock::now();
            for(unsigned i=0;i<16;++i)if(now<until[i])state.buttons|=uint16_t(1u<<i);
        }
        if(current.pending)gamepad.Acknowledge(current);
        return state;
    }
};
std::wstring Wide(const char* value) {
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,nullptr,0);
    if(!size)throw std::invalid_argument("Invalid UTF-8 window title");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,result.data(),size);
    result.pop_back();return result;
}
}
class NativeHost::Impl {
public:
    cave::ScopedTimerResolution timerResolution;
    NativeHostOptions options;
    std::unique_ptr<GsDisplay> display;
    std::shared_ptr<GsCommandQueue> queue;
    std::shared_ptr<Input> input;
    std::function<void()> onFrame;
    std::chrono::steady_clock::time_point nextInput{};
    uint64_t frames=0;
    uint64_t lastOffered=0;
    void Poll() {
        const auto now=std::chrono::steady_clock::now();
        if(now<nextInput)return;
        nextInput=now+2ms;input->Focus(display->Focused());
    }
    ~Impl(){if(queue)queue->Close();if(input)SetHostPadSource({});}
};
NativeHost::NativeHost():impl(std::make_unique<Impl>()){}
NativeHost::~NativeHost()=default;
float NativeHost::Volume() const {return impl->options.volume;}
void NativeHost::Configure(NativeHostOptions options) {
    if(impl->display)throw std::logic_error("Configure the PS2 host before initialization");
    if(options.width<320 || options.width>7680 || options.height<240 || options.height>4320)
        throw std::invalid_argument("Invalid PS2 host window size");
    impl->options=std::move(options);
}
void NativeHost::Initialize(const char* title) {
    StartupTrace("host-init-begin");
    if(impl->display)throw std::logic_error("PS2 host is already initialized");
    // Window presets are physical client pixels, as in the other runtimes.
    // Set this before SDL or Vulkan creates any native window; desktop bitmap
    // scaling would otherwise enlarge 1280x720 to 1920x1080 at 150% DPI.
    using SetDpiContext=BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if(const auto setDpi=reinterpret_cast<SetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext")))
        setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else SetProcessDPIAware();
    // EE deadlines and the 2 ms input wake use timed condition variables.
    // The default Windows timer quantum can round both to about 15.6 ms.
    // Scope the request to this interactive host, including failed startup.
    if(impl->options.timerPrecision)impl->timerResolution.Enable();
    std::cerr<<"[ps2-host] timer_resolution_ms="<<(impl->timerResolution.Active()?1:0)<<'\n';
    auto input=std::make_shared<Input>(impl->options);
    auto display=std::make_unique<GsDisplay>(impl->options.width,impl->options.height,Wide(title),impl->options.vsync,
        impl->options.reflex,impl->options.amdAntiLag);
    if(impl->options.trackFieldBlits)display->Device().EnableFieldBlitTracking();
    auto queue=std::make_shared<GsCommandQueue>(8u*1024u*1024u,2048,display->Device().Completions());
    display->SetKeyHandler([weak=std::weak_ptr<Input>(input)](unsigned key,bool down){
        if(key==VK_F8 && down)std::cerr<<"[ps2-marker] qpc_us="<<frame_trace::Stamp()<<'\n';
        if(auto owner=weak.lock())owner->Key(key,down);
    });
    if(impl->options.fullscreen)display->ToggleFullscreen();
    impl->display=std::move(display);impl->queue=std::move(queue);impl->input=std::move(input);
    SetHostPadSource(impl->input);
    StartupTrace("host-init-complete");
    std::cerr<<"[ps2-host] gpu="<<impl->display->Device().GpuName()<<" input=SDL3 window=Vulkan\n";
}
std::unique_ptr<GsCommandBackend> NativeHost::CreateBackend() {
    if(!impl->queue)throw std::logic_error("Initialize PS2 host before creating the producer");
    std::unique_ptr<GsCommandBackend> backend=std::make_unique<SharedQueue>(impl->queue);
    if(!impl->options.capture.empty())backend=std::make_unique<GsRecordingBackend>(
        std::move(backend),impl->options.capture,impl->options.captureBytes,18000);
    return backend;
}
void NativeHost::BeforeInput(uint64_t field,unsigned budgetUs){impl->display->BeforeInput(field,budgetUs);}
void NativeHost::OfferReadyFrame(PS2Runtime& runtime,uint64_t display2Buffer,unsigned leadUs) {
    const auto& clock=runtime.eeScheduler();
    const auto tick=clock.currentVSyncTick()+1;
    // Both FIFO and early presentation use this same game-side completion.
    impl->display->SimulationReady(tick);
    if(impl->options.vsync || !impl->options.capture.empty())return;
    const auto deadline=clock.nextVBlankDeadline();
    if(tick==impl->lastOffered || deadline-std::chrono::steady_clock::now()<1ms)return;
    auto frame=GsFrame::Snapshot(runtime.memory().gs(),tick,unsigned(tick&1));
    frame.dispfb2=display2Buffer;
    frame.presentAfter=deadline-std::chrono::microseconds(leadUs);
    if(impl->queue->TryFrame(frame))impl->lastOffered=tick;
}
void NativeHost::Run(PS2Runtime& runtime,const std::function<void()>& execute,const std::function<void()>& onFrame) {
    if(!impl->display || impl->queue->GetStats().closed)throw std::logic_error("PS2 host is not ready");
    auto& display=*impl->display;auto mailbox=display.Device().Completions();
    mailbox->SetNotifier([&]{runtime.postEeEvent({EeEventType::GsStatus,0,0});});
    impl->queue->SetFrameNotifier([&]{runtime.postEeEvent({EeEventType::GsStatus,0,0});});
    std::optional<GsFrame> prepared;
    std::optional<GsFrame> repeat;
    std::optional<uint64_t> previousPage;
    bool presentedRepeat=false;
    uint64_t preparedSerial=0,confirmed=0,held=0,corrected=0,repeated=0;
    GsDeviceSink sink(display.Device(),[&](const GsFrame& frame){
        if(frame.Prepared()) {
            if(prepared || !display.Device().IsReadyPage(frame.dispfb2))return;
            repeat.reset();
            if(!display.Present(frame))throw GsWindowClosed();
            presentedRepeat=false;
            prepared=frame;
            preparedSerial=display.Device().ScanoutRevision(frame.dispfb2);
            return;
        }
        repeat.reset();
        auto result=ReadyResult::Correct;
        if(prepared) {
            result=ConfirmReadyFrame(*prepared,frame,previousPage,
                preparedSerial==display.Device().ScanoutRevision(prepared->dispfb2),
                impl->options.trackFieldBlits && !presentedRepeat);
            if(result==ReadyResult::Confirmed)++confirmed;
            else if(result==ReadyResult::Held) {
                ++held;
                repeat=prepared;
                ++repeat->vsyncTick;repeat->field^=1u;
                repeat->presentAfter+=16667us;
            }
            else ++corrected;
        }
        if(result==ReadyResult::Correct && !display.Present(frame))throw GsWindowClosed();
        previousPage=frame.dispfb2;
        prepared.reset();
        ++impl->frames;if(onFrame)onFrame();
    },[&]{display.WaitPreviousFrame();});
    std::atomic<bool> finished=false;std::exception_ptr executionFailure,ownerFailure;
    display.Device().SetWaitPump([&]{
        if(runtime.isStopRequested() || finished.load(std::memory_order_acquire))throw GsQueueClosed();
        if(!display.Alive())throw GsWindowClosed();
        impl->Poll();
    });
    std::thread producer([&]{
        try{execute();}catch(...){executionFailure=std::current_exception();}
        finished.store(true,std::memory_order_release);
    });
    try {
        while(!runtime.isStopRequested() && !finished.load(std::memory_order_acquire)) {
            if(!display.Alive())break;
            impl->Poll();
            auto timeout=2ms;
            if(repeat) {
                const auto remaining=repeat->presentAfter-std::chrono::steady_clock::now();
                if(remaining < -4ms || preparedSerial!=display.Device().ScanoutRevision(repeat->dispfb2)) {
                    repeat.reset();
                } else if(remaining<=1ms) {
                    if(!display.Present(*repeat,true))throw GsWindowClosed();
                    prepared=repeat;
                    // A real worker completion can cover one held field. A
                    // repeat can never extend that lease into another one.
                    presentedRepeat=true;
                    repeat.reset();
                    ++repeated;
                } else timeout=std::min(timeout,std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
            }
            impl->queue->ConsumeOne(sink,timeout);
        }
    }catch(const GsQueueClosed&) {
    }catch(const GsWindowClosed&) {
        // The owner closes the transport before joining a producer that may
        // be blocked on a full queue, initial upload, frame or SIGNAL.
    }catch(...){ownerFailure=std::current_exception();}
    runtime.requestStop();impl->queue->Close(ownerFailure);producer.join();
    std::cerr<<"[ps2-ready-frame] confirmed="<<confirmed<<" held="<<held<<" repeated="<<repeated<<" corrected="<<corrected<<'\n';
    impl->queue->SetFrameNotifier({});mailbox->SetNotifier({});display.Device().SetWaitPump({});SetHostPadSource({});
    const auto stats=impl->queue->GetStats();
    std::cerr<<"[ps2-host] frames="<<impl->frames<<" readbacks="<<display.Device().ReadbackRequests()
        <<" queue_peak_bytes="<<stats.highWaterBytes<<" queue_peak_commands="<<stats.highWaterCommands<<'\n';
    std::cerr<<"[ps2-host-queue] submitted="<<stats.submitted<<" completed="<<stats.completed<<'\n';
    if(ownerFailure)std::rethrow_exception(ownerFailure);
    if(executionFailure) {
        try{std::rethrow_exception(executionFailure);}
        catch(const GsQueueClosed&){}
        catch(const GsWindowClosed&){}
    }
}
}
