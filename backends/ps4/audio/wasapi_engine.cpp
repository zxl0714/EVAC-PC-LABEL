#include "wasapi_engine.h"
#include "../native_diagnostics.h"
#include "../diagnostics/monotonic_clock.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace kt::audio {
namespace {
std::atomic<std::uint64_t> next_stream_id{1};
struct Handle {
    HANDLE value{};
    explicit Handle(bool manual = false) : value(CreateEventW(nullptr, manual, FALSE, nullptr)) {
        if (!value) { throw std::runtime_error("Cannot create an audio event"); }
    }
    ~Handle() { CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
template<typename T> struct Com {
    T* value{};
    ~Com() { Reset(); }
    void Reset() { if (value) { value->Release(); value = nullptr; } }
    T** Out() { Reset(); return &value; }
    T* operator->() const { return value; }
};
struct Failure { HRESULT result; };
void Check(HRESULT result) { if (FAILED(result)) { throw Failure{result}; } }
void HostError(const char* operation, HRESULT result) {
    std::printf("{\"event\":\"native_audio_host_error\",\"operation\":\"%s\",\"hresult\":\"0x%08lx\"}\n", operation, static_cast<DWORD>(result));
}
float Sample(const unsigned char* input, bool floating) {
    if (floating) {
        float value{}; std::memcpy(&value, input, 4);
        return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
    }
    std::int16_t value{}; std::memcpy(&value, input, 2);
    return static_cast<float>(value) / 32768.0f;
}
std::vector<float> Convert(Configuration configuration, const void* data) {
    const auto count = static_cast<std::size_t>(configuration.frames) * configuration.channels;
    std::vector<float> output(count);
    const auto* bytes = static_cast<const unsigned char*>(data);
    const auto stride = configuration.floating ? 4 : 2;
    for (std::size_t i = 0; i < count; ++i) { output[i] = Sample(bytes + i * stride, configuration.floating); }
    return output;
}
void ApplyGain(float* destination, const std::vector<float>& packet, std::size_t offset, unsigned frames,
               Configuration configuration, const std::array<int, 8>& volume) {
    const auto channels = configuration.channels;
    // Windows 7.1: FL FR FC LFE BL BR SL SR. The PS4 legacy order and
    // volume bits put SL/SR before BL/BR; Std input is already Windows order.
    constexpr std::array<unsigned, 8> legacy_from_windows{0, 1, 2, 3, 6, 7, 4, 5};
    for (unsigned frame = 0; frame < frames; ++frame) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            const auto gain_channel = channels == 8 ? legacy_from_windows[channel] : channel;
            const auto input_channel = channels == 8 && !configuration.standard8 ? gain_channel : channel;
            destination[frame * channels + channel] = packet[(offset + frame) * channels + input_channel] *
                (static_cast<float>(volume[gain_channel]) / 32768.0f) * configuration.master_gain;
        }
    }
}
}

struct Port {
    explicit Port(Configuration value, HANDLE wake_event) : configuration(value), wake(wake_event) {
        volume.fill(32768); status.stream_id = next_stream_id.fetch_add(1, std::memory_order_relaxed);
    }
    Configuration configuration;
    HANDLE wake; // owned by the engine; engine retains every live port
    std::mutex mutex, submission;
    std::condition_variable changed;
    std::vector<float> packet;
    std::size_t offset{};
    std::array<int, 8> volume{};
    bool open_done{};
    std::uint64_t report_clock_us{}, reported_underruns{};
    Snapshot status;
};

namespace {
void Retire(const std::shared_ptr<Port>& port, HRESULT result = S_OK) {
    std::lock_guard lock(port->mutex);
    port->status.closed = port->status.retired = true;
    if (FAILED(result)) { port->status.failure = result; }
    port->packet.clear(); port->status.pending = false; port->open_done = true;
    port->changed.notify_all();
}
struct DeviceStream {
    std::shared_ptr<Port> port;
    Handle ready;
    Com<IAudioClient> client;
    Com<IAudioRenderClient> render;
    unsigned capacity{}, target{};
    bool started{}, starved{}, was_idle = true;
    std::uint64_t last_pump_clock_us{};
    std::chrono::steady_clock::time_point progress = std::chrono::steady_clock::now();
    DeviceStream(const std::shared_ptr<Port>& value, IMMDevice* device) : port(value) {
        Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.Out())));
        WAVEFORMATEXTENSIBLE extended{};
        auto& format = extended.Format;
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = static_cast<WORD>(port->configuration.channels);
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * 4);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        if (format.nChannels == 8) {
            format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; format.cbSize = 22;
            extended.Samples.wValidBitsPerSample = 32;
            extended.dwChannelMask = 0x63f;
            extended.SubFormat = {WAVE_FORMAT_IEEE_FLOAT, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        }
        Check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 0, 0, &format, nullptr));
        // Set the event immediately: even partial initialization teardown must
        // not leave an event-driven client waiting for an unbound handle.
        Check(client->SetEventHandle(ready.value));
        Check(client->GetBufferSize(&capacity));
        if (!capacity || capacity > 48000) { throw Failure{E_UNEXPECTED}; }
        REFERENCE_TIME period{}, minimum{};
        Check(client->GetDevicePeriod(&period, &minimum));
        if (period <= 0 || period > 10000000) { throw Failure{E_UNEXPECTED}; }
        const auto period_frames = static_cast<unsigned>((static_cast<std::uint64_t>(period) * 48000 + 9999999) / 10000000);
        target = (std::min)(capacity, (std::max)(port->configuration.frames, period_frames * 2));
        Check(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.Out())));
        { std::lock_guard lock(port->mutex); port->status.device_capacity = capacity; }
        std::printf("{\"event\":\"native_audio_stream\",\"rate\":48000,\"channels\":%u,\"device_capacity\":%u,\"target_frames\":%u}\n",
            port->configuration.channels, capacity, target);
    }
    ~DeviceStream() {
        if (client.value) { (void)client->Stop(); (void)client->Reset(); }
        // COM services are destroyed on this worker before its apartment ends.
    }
    void Pump() {
        const auto pump_clock = kt::diagnostics::MonotonicMicroseconds();
        UINT32 padding{}; Check(client->GetCurrentPadding(&padding));
        if (padding > capacity) { throw Failure{E_UNEXPECTED}; }
        std::unique_lock lock(port->mutex);
        auto& status = port->status;
        if (last_pump_clock_us && !was_idle) {
            status.max_pump_gap_us = (std::max)(status.max_pump_gap_us, pump_clock - last_pump_clock_us);
        }
        last_pump_clock_us = pump_clock;
        const auto previous_consumed = status.consumed_frames;
        status.padding = padding;
        status.consumed_frames = (std::max)(status.consumed_frames,
            status.written_frames >= padding ? status.written_frames - padding : 0);
        const bool active_work = padding || !port->packet.empty();
        if (!active_work || was_idle) { progress = std::chrono::steady_clock::now(); }
        was_idle = !active_work;
        if (previous_consumed != status.consumed_frames) { progress = std::chrono::steady_clock::now(); }
        if (active_work && std::chrono::steady_clock::now() - progress > std::chrono::seconds(2)) {
            throw Failure{HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        if (port->packet.empty()) {
            if (started && !padding && !starved) {
                ++status.underruns; status.last_underrun_clock_us = pump_clock; starved = true;
            }
            port->changed.notify_all(); return;
        }
        starved = false;
        if (padding >= target) { return; }
        const auto remaining = port->configuration.frames - static_cast<unsigned>(port->offset);
        const auto frames = (std::min)({remaining, capacity - padding, target - padding});
        if (!frames) { return; }
        BYTE* buffer{}; Check(render->GetBuffer(frames, &buffer));
        auto* samples = reinterpret_cast<float*>(buffer);
        ApplyGain(samples, port->packet, port->offset, frames, port->configuration, port->volume);
        Check(render->ReleaseBuffer(frames, 0));
        status.written_frames += frames; status.padding = padding + frames;
        progress = std::chrono::steady_clock::now();
        port->offset += frames;
        if (port->offset == port->configuration.frames) { port->packet.clear(); port->offset = 0; status.pending = false; }
        if (!started) { Check(client->Start()); started = true; }
        port->changed.notify_all();
    }
};
}

struct WasapiEngine::Implementation {
    Handle stop{true}, work, ready{true};
    std::thread thread;
    std::mutex commands, lifecycle;
    std::deque<std::shared_ptr<Port>> pending;
    std::atomic<bool> stopping{};
    std::atomic<long> initialization{E_PENDING};
    std::array<std::unique_ptr<DeviceStream>, 16> streams;
    ~Implementation() { Stop(); }
    void Stop() {
        std::lock_guard lock(lifecycle);
        stopping.store(true); SetEvent(stop.value);
        if (thread.joinable()) { thread.join(); }
    }
    void Run() noexcept {
        const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment)) { initialization.store(apartment); SetEvent(ready.value); return; }
        {
            Com<IMMDeviceEnumerator> devices;
            try {
                Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(devices.Out())));
                Com<IMMDevice> device;
                Check(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, device.Out()));
                Com<IAudioClient> probe;
                Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(probe.Out())));
                WAVEFORMATEX* mix{}; Check(probe->GetMixFormat(&mix));
                if (!mix) { throw Failure{E_UNEXPECTED}; }
                std::printf("{\"event\":\"native_audio_endpoint\",\"backend\":\"wasapi_shared\",\"mix_rate\":%lu,\"mix_channels\":%u,\"mix_bits\":%u}\n",
                    mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample);
                CoTaskMemFree(mix);
                initialization.store(S_OK); SetEvent(ready.value);
                while (!stopping.load()) {
                    {
                        std::deque<std::shared_ptr<Port>> requests;
                        { std::lock_guard lock(commands); requests.swap(pending); }
                        for (auto& port : requests) {
                            auto slot = std::find_if(streams.begin(), streams.end(), [](const auto& value) { return !value; });
                            if (slot == streams.end()) { Retire(port, E_OUTOFMEMORY); continue; }
                            try {
                                Com<IMMDevice> output;
                                Check(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, output.Out()));
                                *slot = std::make_unique<DeviceStream>(port, output.value);
                                { std::lock_guard lock(port->mutex); port->open_done = true; port->changed.notify_all(); }
                            } catch (const Failure& error) { HostError("open", error.result); Retire(port, error.result); }
                            catch (const std::bad_alloc&) { Retire(port, E_OUTOFMEMORY); }
                            catch (...) { HostError("open_exception", E_FAIL); Retire(port, E_FAIL); }
                        }
                    }
                    std::array<HANDLE, 18> events{stop.value, work.value}; unsigned count = 2;
                    for (auto& stream : streams) {
                        if (!stream) { continue; }
                        bool closed{}; { std::lock_guard lock(stream->port->mutex); closed = stream->port->status.closed; }
                        if (closed) { auto port = stream->port; stream.reset(); Retire(port); continue; }
                        try { stream->Pump(); }
                        catch (const Failure& error) { HostError("render", error.result); auto port = stream->port; stream.reset(); Retire(port, error.result); continue; }
                        events[count++] = stream->ready.value;
                    }
                    const auto signaled = MsgWaitForMultipleObjectsEx(count, events.data(), count > 2 ? 100 : INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (signaled == WAIT_FAILED) { throw Failure{HRESULT_FROM_WIN32(GetLastError())}; }
                    if (signaled == WAIT_OBJECT_0 + count) {
                        MSG message;
                        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                    }
                }
            } catch (const Failure& error) { HostError("engine", error.result); initialization.store(error.result); SetEvent(ready.value); }
            catch (...) { HostError("engine_exception", E_FAIL); initialization.store(E_FAIL); SetEvent(ready.value); }
            stopping.store(true);
            for (auto& stream : streams) {
                if (stream) { auto port = stream->port; stream.reset(); Retire(port, E_ABORT); }
            }
            std::lock_guard lock(commands);
            for (auto& port : pending) { Retire(port, E_ABORT); }
            pending.clear();
        }
        CoUninitialize();
    }
};

WasapiEngine::WasapiEngine() : impl_(std::make_unique<Implementation>()) {
    auto* implementation = impl_.get();
    impl_->thread = std::thread([implementation] { implementation->Run(); });
    if (WaitForSingleObject(impl_->ready.value, 5000) != WAIT_OBJECT_0) { UnsupportedApi("native_audio_start_timeout", 0, nullptr); }
}
WasapiEngine::~WasapiEngine() = default;
long WasapiEngine::ReadyResult() const { return impl_->initialization.load(); }
void WasapiEngine::Stop() { impl_->Stop(); }
std::shared_ptr<Port> WasapiEngine::Open(Configuration configuration, long& result) {
    if (!configuration.frames || configuration.frames > 2048 ||
        (configuration.channels != 1 && configuration.channels != 2 && configuration.channels != 8)) { result = E_INVALIDARG; return {}; }
    auto port = std::make_shared<Port>(configuration, impl_->work.value);
    {
        std::lock_guard lock(impl_->commands);
        if (impl_->stopping.load() || FAILED(impl_->initialization.load())) { result = E_ABORT; return {}; }
        if (impl_->pending.size() >= 16) { result = E_OUTOFMEMORY; return {}; }
        impl_->pending.push_back(port); SetEvent(impl_->work.value);
    }
    std::unique_lock lock(port->mutex);
    if (!port->changed.wait_for(lock, std::chrono::seconds(5), [&] { return port->open_done; })) { UnsupportedApi("native_audio_open_timeout", 0, nullptr); }
    result = port->status.failure;
    return FAILED(result) ? nullptr : port;
}
long WasapiEngine::Submit(const std::shared_ptr<Port>& port, const void* data) {
    std::lock_guard serial(port->submission);
    const auto arrived = kt::diagnostics::MonotonicMicroseconds();
    std::unique_lock lock(port->mutex);
    ++port->status.waiting;
    port->changed.wait(lock, [&] { return port->status.closed || FAILED(port->status.failure) ||
        (port->packet.empty() && (data || port->status.padding == 0)); });
    --port->status.waiting;
    if (FAILED(port->status.failure)) { return port->status.failure; }
    if (port->status.closed) { return E_ABORT; }
    if (!data) { return S_OK; }
    const auto accepted = kt::diagnostics::MonotonicMicroseconds();
    auto converted = Convert(port->configuration, data);
    port->packet = std::move(converted); port->offset = 0; port->status.pending = true;
    port->status.submitted_frames += port->configuration.frames;
    auto& status = port->status;
    ++status.submissions;
    status.submit_gap_us = status.last_submit_clock_us ? accepted - status.last_submit_clock_us : 0;
    status.submit_wait_us = accepted - arrived;
    status.max_submit_gap_us = (std::max)(status.max_submit_gap_us, status.submit_gap_us);
    status.max_submit_wait_us = (std::max)(status.max_submit_wait_us, status.submit_wait_us);
    status.last_submit_clock_us = accepted;
    const bool report = !port->report_clock_us || accepted - port->report_clock_us >= 1000000 ||
        (status.underruns != port->reported_underruns && accepted - port->report_clock_us >= 100000);
    const auto snapshot = status;
    const auto pending_frames = port->configuration.frames - static_cast<unsigned>(port->offset);
    if (report) { port->report_clock_us = accepted; port->reported_underruns = status.underruns; }
    SetEvent(port->wake);
    lock.unlock();
    // Never perform log I/O while holding the mutex needed by the audio worker.
    // One summary per second, or at most ten per second during underruns.
    if (report) {
        std::printf("{\"event\":\"native_audio_timing\",\"stream\":%llu,\"clock_us\":%llu,\"thread\":%lu,\"rate\":48000,\"frames\":%u,\"channels\":%u,\"submissions\":%llu,\"submitted_frames\":%llu,\"written_frames\":%llu,\"consumed_frames\":%llu,\"padding\":%u,\"pending_frames\":%u,\"device_capacity\":%u,\"submit_gap_us\":%llu,\"submit_wait_us\":%llu,\"max_submit_gap_us\":%llu,\"max_submit_wait_us\":%llu,\"max_pump_gap_us\":%llu,\"underruns\":%llu,\"last_underrun_clock_us\":%llu}\n",
            snapshot.stream_id, accepted, GetCurrentThreadId(), port->configuration.frames, port->configuration.channels, snapshot.submissions,
            snapshot.submitted_frames, snapshot.written_frames, snapshot.consumed_frames, snapshot.padding, pending_frames,
            snapshot.device_capacity, snapshot.submit_gap_us, snapshot.submit_wait_us, snapshot.max_submit_gap_us,
            snapshot.max_submit_wait_us, snapshot.max_pump_gap_us, snapshot.underruns, snapshot.last_underrun_clock_us);
    }
    return S_OK;
}
long WasapiEngine::Volume(const std::shared_ptr<Port>& port, unsigned flags, int value) {
    if (flags & ~255u || value < 0 || value > 32768) { return E_INVALIDARG; }
    std::lock_guard lock(port->mutex);
    if (FAILED(port->status.failure)) { return port->status.failure; }
    if (port->status.closed) { return E_ABORT; }
    for (unsigned i = 0; i < 8; ++i) { if (flags & (1u << i)) { port->volume[i] = value; } }
    return S_OK;
}
long WasapiEngine::Close(const std::shared_ptr<Port>& port) {
    std::unique_lock lock(port->mutex);
    if (port->status.retired) { return S_OK; }
    port->status.closed = true; port->changed.notify_all(); SetEvent(port->wake);
    if (!port->changed.wait_for(lock, std::chrono::seconds(5), [&] { return port->status.retired; })) { UnsupportedApi("native_audio_close_timeout", 0, nullptr); }
    return S_OK;
}
Snapshot WasapiEngine::Inspect(const std::shared_ptr<Port>& port) { std::lock_guard lock(port->mutex); return port->status; }

bool AudioConversionSelfTest() {
    const std::int16_t pcm[]{-32768, 32767, 0, 16384};
    const auto samples = Convert({2, 2, false}, pcm);
    const float input[]{-2.0f, 0.25f, 2.0f, (std::numeric_limits<float>::quiet_NaN)()};
    const auto floats = Convert({2, 2, true}, input);
    std::array<float, 5> scaled{0, 0, 0, 0, 3};
    const std::array<int, 8> gain{16384, 8192};
    ApplyGain(scaled.data(), samples, 0, 2, {2, 2, false}, gain);
    const std::vector<float> eight{0, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f};
    const std::array<int, 8> volumes{32768, 32768, 32768, 32768, 16384, 8192, 4096, 2048};
    std::array<float, 9> legacy{}, standard{}; legacy.back() = standard.back() = 73;
    ApplyGain(legacy.data(), eight, 0, 1, {1, 8, true, false}, volumes);
    ApplyGain(standard.data(), eight, 0, 1, {1, 8, true, true}, volumes);
    std::array<float, 4> master{}, muted{};
    ApplyGain(master.data(), samples, 0, 2, {2, 2, false, false, 0.25f}, gain);
    ApplyGain(muted.data(), samples, 0, 2, {2, 2, false, false, 0.0f}, gain);
    return samples[0] == -1 && samples[1] == 32767.0f / 32768 && samples[2] == 0 && samples[3] == 0.5f &&
        floats[0] == -1 && floats[1] == 0.25f && floats[2] == 1 && floats[3] == 0 && scaled[0] == -0.5f &&
        scaled[1] == samples[1] * 0.25f && scaled[2] == 0 && scaled[3] == 0.125f && scaled[4] == 3 &&
        legacy[4] == eight[6] * 0.125f && legacy[6] == eight[4] * 0.5f && legacy[8] == 73 &&
        standard[4] == eight[4] * 0.125f && standard[6] == eight[6] * 0.5f && standard[8] == 73 &&
        master[0] == -0.125f && master[3] == 0.03125f &&
        std::all_of(muted.begin(), muted.end(), [](float value) { return value == 0; });
}
}
