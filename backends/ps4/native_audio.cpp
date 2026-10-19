#include "native_audio.h"
#include "native_diagnostics.h"
#include "native_time.h"
#include "native_user.h"
#include "audio/wasapi_engine.h"
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
constexpr int Error(unsigned code) { return static_cast<int>(0x80260000u | code); }
constexpr int kNotOpened = Error(1), kPort = Error(3), kPointer = Error(4), kFull = Error(5), kSize = Error(6);
constexpr int kFormat = Error(7), kRate = Error(8), kVolume = Error(9), kType = Error(10), kMemory = Error(13);
constexpr int kAlready = Error(14), kNotInit = Error(15), kResource = Error(17), kFlag = Error(19), kArgument = Error(21);
using Engine = kt::audio::WasapiEngine;
struct AudioPort { std::shared_ptr<kt::audio::Port> host; unsigned frames, channels; };
struct AudioState {
    std::mutex mutex;
    std::shared_ptr<Engine> engine;
    std::map<int, AudioPort> ports;
    int next = 1;
};
// Lifetime precedes main; the Windows worker is stopped before CRT teardown.
AudioState state;
template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { UnsupportedApi("native_audio_host_exception", 0, caller); }
}
int HostResult(long result) { return result == E_ABORT ? kNotOpened : result == E_OUTOFMEMORY ? kMemory : FAILED(result) ? kResource : 0; }
int Initialize(const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex);
        if (state.engine) { return kAlready; }
        auto candidate = std::make_shared<Engine>();
        const auto result = candidate->ReadyResult();
        if (FAILED(result)) { return HostResult(result); }
        state.engine = std::move(candidate);
        TraceNativeApi("sceAudioOutInit", 0, caller);
        return 0;
    });
}
int Find(int handle, AudioPort& port) {
    std::lock_guard lock(state.mutex);
    if (!state.engine) { return kNotInit; }
    const auto entry = state.ports.find(handle);
    if (entry == state.ports.end()) { return handle > 0 && handle < state.next ? kNotOpened : kPort; }
    port = entry->second; return 0;
}
int Open(int user, int type, int index, unsigned frames, unsigned rate, unsigned format, const void* caller) {
    return Api(caller, [&] {
        std::lock_guard lock(state.mutex);
        if (!state.engine) { return kNotInit; }
        if (user != 0xff && (user != 0x1000 || !NativeUserInitialized())) { return kArgument; }
        if (!frames || frames > 2048 || frames % 256) { return kSize; }
        if (rate != 48000) { return kRate; }
        if (index) { return kArgument; }
        if ((type < 0 || type > 4) && type != 126 && type != 127) { return kType; }
        if (type > 2) { UnsupportedApi("sceAudioOutOpen_port_type", static_cast<unsigned>(type), caller); }
        if ((format & 255) > 7) { return kFormat; }
        if (format & ~255u) { UnsupportedApi("sceAudioOutOpen_attributes", format, caller); }
        const bool floating = format == 3 || format == 4 || format == 5 || format == 7;
        const unsigned channels = format == 0 || format == 3 ? 1 : format == 1 || format == 4 ? 2 : 8;
        if (state.ports.size() >= 16 || state.next == (std::numeric_limits<int>::max)()) { return kFull; }
        long result{};
        auto port = state.engine->Open({frames, channels, floating, format == 6 || format == 7}, result);
        if (!port) { return result == E_ABORT ? kResource : HostResult(result); }
        const auto handle = state.next;
        try { state.ports.emplace(handle, AudioPort{port, frames, channels}); }
        catch (...) { (void)Engine::Close(port); throw; }
        ++state.next;
        std::printf("{\"event\":\"native_audio_open\",\"handle\":%d,\"frames\":%u,\"channels\":%u,\"format\":%u}\n", handle, frames, channels, format);
        TraceNativeApi("sceAudioOutOpen", static_cast<unsigned>(handle), caller);
        return handle;
    });
}
int Output(int handle, const void* data, const void* caller) {
    return Api(caller, [&] {
        AudioPort port; const auto found = Find(handle, port); if (found) { return found; }
        const auto result = Engine::Submit(port.host, data);
        if (FAILED(result)) { return HostResult(result); }
        return data ? static_cast<int>(port.frames * port.channels) : 0;
    });
}
int Volume(int handle, unsigned flags, const int* volume, const void* caller) {
    return Api(caller, [&] {
        AudioPort port; const auto found = Find(handle, port); if (found) { return found; }
        if (!volume) { return kPointer; }
        if (*volume < 0 || *volume > 32768) { return kVolume; }
        if (flags & ~255u) { return kFlag; }
        return HostResult(Engine::Volume(port.host, flags, *volume));
    });
}
int Close(int handle, const void* caller) {
    return Api(caller, [&] {
        AudioPort port;
        {
            std::lock_guard lock(state.mutex);
            if (!state.engine) { return kNotInit; }
            const auto entry = state.ports.find(handle);
            if (entry == state.ports.end()) { return handle > 0 && handle < state.next ? kNotOpened : kPort; }
            port = entry->second; state.ports.erase(entry);
        }
        const auto result = Engine::Close(port.host);
        const auto status = Engine::Inspect(port.host);
        std::printf("{\"event\":\"native_audio_close\",\"handle\":%d,\"submitted_frames\":%llu,\"written_frames\":%llu,\"consumed_frames\":%llu,\"underruns\":%llu}\n",
            handle, status.submitted_frames, status.written_frames, status.consumed_frames, status.underruns);
        return HostResult(result);
    });
}
int KT_SYSV AudioInit() { return Initialize(__builtin_return_address(0)); }
int KT_SYSV AudioOpen(int user, int type, int index, unsigned frames, unsigned rate, unsigned format) { return Open(user, type, index, frames, rate, format, __builtin_return_address(0)); }
int KT_SYSV AudioOutput(int handle, const void* data) { return Output(handle, data, __builtin_return_address(0)); }
int KT_SYSV AudioVolume(int handle, unsigned flags, const int* volume) { return Volume(handle, flags, volume, __builtin_return_address(0)); }
int KT_SYSV AudioClose(int handle) { return Close(handle, __builtin_return_address(0)); }
}

std::uint64_t ResolveAudioImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceAudioOut" || module != "libSceAudioOut") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "JfEPXVxhFqA") { return reinterpret_cast<std::uint64_t>(&AudioInit); }
    if (id == "ekNvsT22rsY") { return reinterpret_cast<std::uint64_t>(&AudioOpen); }
    if (id == "s1--uE9mBFw") { return reinterpret_cast<std::uint64_t>(&AudioClose); }
    if (id == "QOQtbeDqsT4") { return reinterpret_cast<std::uint64_t>(&AudioOutput); }
    if (id == "b+uAV89IlxE") { return reinterpret_cast<std::uint64_t>(&AudioVolume); }
    return 0;
}
bool AudioSelfTest() {
    bool okay = true; unsigned passed{};
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"audio_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        okay &= value; if (value) { ++passed; }
    };
    check(Output(1, nullptr, nullptr) == kNotInit && Close(1, nullptr) == kNotInit, "before_initialization");
    check(kt::audio::AudioConversionSelfTest(), "pcm_float_per_channel_gain_and_finite_clamping");
    const auto initialized = Initialize(nullptr);
    check(initialized == 0 && Initialize(nullptr) == kAlready, "real_wasapi_endpoint_initialized");
    if (initialized) { return false; }
    check(Open(0xff, 0, 0, 1, 48000, 1, nullptr) == kSize && Open(0xff, 0, 0, 256, 44100, 1, nullptr) == kRate &&
          Open(0xff, 0, 0, 256, 48000, 255, nullptr) == kFormat, "invalid_configuration_not_published");
    const auto handle = Open(0xff, 0, 0, 256, 48000, 1, nullptr);
    check(handle > 0, "real_stereo_stream_opened");
    if (handle <= 0) { return false; }
    AudioPort port; (void)Find(handle, port);
    int volume = 16384;
    check(Volume(handle, 3, &volume, nullptr) == 0 && Volume(handle, 3, nullptr, nullptr) == kPointer &&
          Volume(handle, 256, &volume, nullptr) == kFlag, "per_port_volume_validation");
    std::array<std::int16_t, 512> silence{};
    const auto before = NativeProcessMicroseconds();
    for (int i = 0; i < 12; ++i) { okay &= Output(handle, silence.data(), nullptr) == 512; }
    const auto drained = Output(handle, nullptr, nullptr);
    const auto status = Engine::Inspect(port.host);
    check(drained == 0 && status.submitted_frames == 3072 && status.written_frames == 3072 && status.consumed_frames == 3072 &&
          status.padding == 0 && NativeProcessMicroseconds() - before >= 40000, "real_device_progress_and_bounded_output_pacing");
    check(status.submissions == 12 && status.last_submit_clock_us > 0 && status.max_submit_gap_us >= status.submit_gap_us &&
          status.max_submit_wait_us >= status.submit_wait_us, "audio_timing_preserves_real_submission_and_device_counters");
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    check(Output(handle, silence.data(), nullptr) == 512 && Output(handle, nullptr, nullptr) == 0 &&
          Engine::Inspect(port.host).failure == 0, "idle_longer_than_stall_limit_can_resume");
    check(Close(handle, nullptr) == 0 && Engine::Inspect(port.host).retired && Output(handle, nullptr, nullptr) == kNotOpened &&
          Close(handle, nullptr) == kNotOpened, "close_retires_resources_and_rejects_stale_handle");
    const auto cancel_handle = Open(0xff, 0, 0, 2048, 48000, 1, nullptr);
    AudioPort cancel_port; const bool cancel_ready = cancel_handle > 0 && Find(cancel_handle, cancel_port) == 0;
    int producer_result{}; bool observed_wait = false;
    if (cancel_ready) {
        std::array<std::int16_t, 4096> large_silence{};
        std::thread producer([&] {
            for (unsigned i = 0; i < 100; ++i) {
                producer_result = Output(cancel_handle, large_silence.data(), nullptr);
                if (producer_result < 0) { break; }
            }
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            const auto current = Engine::Inspect(cancel_port.host);
            if (current.waiting && current.pending) { observed_wait = true; break; }
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        const auto closed = Close(cancel_handle, nullptr);
        producer.join();
        check(observed_wait && closed == 0 && producer_result == kNotOpened && Engine::Inspect(cancel_port.host).retired,
              "close_cancels_a_verified_blocked_producer");
    } else { check(false, "close_cancels_a_verified_blocked_producer"); }
    const auto float_handle = Open(0xff, 1, 0, 256, 48000, 3, nullptr);
    std::array<float, 256> float_silence{};
    check(float_handle > 0 && Output(float_handle, float_silence.data(), nullptr) == 256 && Output(float_handle, nullptr, nullptr) == 0 &&
          Close(float_handle, nullptr) == 0, "mono_float_shared_stream");
    bool multichannel = true;
    std::array<float, 2048> eight_silence{};
    for (const auto format : {2u, 5u, 6u, 7u}) {
        const auto eight = Open(0xff, 0, 0, 256, 48000, format, nullptr);
        AudioPort active;
        if (eight <= 0 || Find(eight, active) != 0) { multichannel = false; continue; }
        const auto submitted = Output(eight, eight_silence.data(), nullptr), drain = Output(eight, nullptr, nullptr);
        const auto progress = Engine::Inspect(active.host);
        multichannel &= submitted == 2048 && drain == 0 && progress.submitted_frames == 256 && progress.consumed_frames == 256;
        multichannel &= Close(eight, nullptr) == 0;
    }
    check(multichannel, "eight_channel_legacy_and_standard_pcm_float_real_device_consumption");
    state.engine->Stop();
    check(Engine::Inspect(port.host).retired, "worker_shutdown_after_close");
    std::printf("{\"event\":\"audio_self_test\",\"passed\":%u,\"success\":%s}\n", passed, okay ? "true" : "false");
    return okay;
}
