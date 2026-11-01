#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_host.h"
#include "host_input.h"
#include "gs_display.h"
#include "gs_command_queue.h"
#include "gs_completion_mailbox.h"
#include "gs_device_sink.h"
#include "gs_trace.h"
#include "../ps4/input/keyboard_config.h"
#include "../ps4/input/sdl_gamepad.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <Windows.h>
#include "../../common/windows_timer_resolution.h"
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
    uint16_t held=0,pending=0;
    kt::input::GamepadSnapshot controller{};
    std::mutex mutex;
    bool focused=false;
public:
    explicit Input(const std::filesystem::path& config) {
        bindings=config.empty()?kt::input::DefaultKeyboardBindings():kt::input::LoadKeyboardConfig(config);
        gamepad.Configure(true,0.25);gamepad.Initialize();
    }
    void Key(unsigned key,bool down) {
        if(key>=keys.size())return;
        std::lock_guard lock(mutex);
        if(keys[key]==down)return;
        keys[key]=down;
        const auto mask=Ps2Buttons(bindings[key]);
        if(down) {
            pending|=mask;
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
    void Poll(bool focus) {
        gamepad.Focus(focus);
        auto snapshot=gamepad.Poll();
        std::lock_guard lock(mutex);
        if(!focus){keys.fill(false);held=0;pending=0;until.fill({});}
        focused=focus;controller=snapshot;
    }
    HostPadState Read(unsigned port) override {
        if(port)return {};
        kt::input::GamepadSnapshot delivered;HostPadState state;
        {
            std::lock_guard lock(mutex);
            if(!focused)return {};
            state.buttons=held|pending;
            const auto now=std::chrono::steady_clock::now();
            for(unsigned i=0;i<16;++i)if(now<until[i])state.buttons|=uint16_t(1u<<i);
            pending=0;
            delivered=controller;
            state.buttons|=Ps2Buttons(delivered.buttons|delivered.pending);
            state.axes={delivered.right_x,delivered.right_y,delivered.left_x,delivered.left_y};
        }
        gamepad.Acknowledge(delivered);
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
    void Poll() {
        const auto now=std::chrono::steady_clock::now();
        if(now<nextInput)return;
        nextInput=now+2ms;input->Poll(display->Focused());
    }
    ~Impl(){if(queue)queue->Close();if(input)SetHostPadSource({});}
};
NativeHost::NativeHost():impl(std::make_unique<Impl>()){}
NativeHost::~NativeHost()=default;
void NativeHost::Configure(NativeHostOptions options) {
    if(impl->display)throw std::logic_error("Configure the PS2 host before initialization");
    if(options.width<320 || options.width>7680 || options.height<240 || options.height>4320)
        throw std::invalid_argument("Invalid PS2 host window size");
    impl->options=std::move(options);
}
void NativeHost::Initialize(const char* title) {
    if(impl->display)throw std::logic_error("PS2 host is already initialized");
    // EE deadlines and the 2 ms input wake use timed condition variables.
    // The default Windows timer quantum can round both to about 15.6 ms.
    // Scope the request to this interactive host, including failed startup.
    if(impl->options.timerPrecision)impl->timerResolution.Enable();
    std::cerr<<"[ps2-host] timer_resolution_ms="<<(impl->timerResolution.Active()?1:0)<<'\n';
    auto input=std::make_shared<Input>(impl->options.keyboardConfig);
    auto display=std::make_unique<GsDisplay>(impl->options.width,impl->options.height,Wide(title),impl->options.vsync);
    auto queue=std::make_shared<GsCommandQueue>(8u*1024u*1024u,2048,display->Device().Completions());
    display->SetKeyHandler([weak=std::weak_ptr<Input>(input)](unsigned key,bool down){if(auto owner=weak.lock())owner->Key(key,down);});
    if(impl->options.fullscreen)display->ToggleFullscreen();
    impl->display=std::move(display);impl->queue=std::move(queue);impl->input=std::move(input);
    SetHostPadSource(impl->input);
    std::cerr<<"[ps2-host] gpu="<<impl->display->Device().GpuName()<<" input=SDL3 window=Vulkan\n";
}
std::unique_ptr<GsCommandBackend> NativeHost::CreateBackend() {
    if(!impl->queue)throw std::logic_error("Initialize PS2 host before creating the producer");
    std::unique_ptr<GsCommandBackend> backend=std::make_unique<SharedQueue>(impl->queue);
    if(!impl->options.capture.empty())backend=std::make_unique<GsRecordingBackend>(
        std::move(backend),impl->options.capture,512u*1024u*1024u,18000);
    return backend;
}
void NativeHost::Run(PS2Runtime& runtime,const std::function<void()>& execute,const std::function<void()>& onFrame) {
    if(!impl->display || impl->queue->GetStats().closed)throw std::logic_error("PS2 host is not ready");
    auto& display=*impl->display;auto mailbox=display.Device().Completions();
    mailbox->SetNotifier([&]{runtime.postEeEvent({EeEventType::GsStatus,0,0});});
    impl->queue->SetFrameNotifier([&]{runtime.postEeEvent({EeEventType::GsStatus,0,0});});
    GsDeviceSink sink(display.Device(),[&](const GsFrame& frame){
        if(!display.Present(frame))throw GsWindowClosed();
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
            impl->Poll();impl->queue->ConsumeOne(sink,2ms);
        }
    }catch(const GsQueueClosed&) {
    }catch(const GsWindowClosed&) {
        // The owner closes the transport before joining a producer that may
        // be blocked on a full queue, initial upload, frame or SIGNAL.
    }catch(...){ownerFailure=std::current_exception();}
    runtime.requestStop();impl->queue->Close(ownerFailure);producer.join();
    impl->queue->SetFrameNotifier({});mailbox->SetNotifier({});display.Device().SetWaitPump({});SetHostPadSource({});
    const auto stats=impl->queue->GetStats();
    std::cerr<<"[ps2-host] frames="<<impl->frames<<" readbacks="<<display.Device().ReadbackRequests()
        <<" queue_peak_bytes="<<stats.highWaterBytes<<" queue_peak_commands="<<stats.highWaterCommands<<'\n';
    if(ownerFailure)std::rethrow_exception(ownerFailure);
    if(executionFailure) {
        try{std::rethrow_exception(executionFailure);}
        catch(const GsQueueClosed&){}
        catch(const GsWindowClosed&){}
    }
}
}
