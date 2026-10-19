#include "native_avplayer.h"
#include "native_diagnostics.h"
#include "native_content.h"
#include "native_memory.h"
#include "media/mf_reader.h"
#include "media/playback_session.h"
#include <windows.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <array>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <future>
#include <functional>
#include <deque>
#include <cmath>
#include <atomic>
#include <optional>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
using Handle = std::uint64_t;
using Allocate = void* (KT_SYSV*)(void*, unsigned, unsigned);
using Release = void (KT_SYSV*)(void*, void*);
using Event = void (KT_SYSV*)(void*, unsigned, int, void*);
struct Allocator { void* object; Allocate allocate; Release release; Allocate texture; Release release_texture; };
struct FileReplacement { void* object; std::uintptr_t open, close, read, size; };
struct EventReplacement { void* object; Event callback; };
struct InitData {
    Allocator memory;
    FileReplacement file;
    EventReplacement event;
    unsigned debug_level, priority;
    int buffers;
    unsigned char auto_start, reserved[3];
    const char* language;
};
static_assert(sizeof(InitData) == 120 && offsetof(InitData, file) == 40 && offsetof(InitData, event) == 80);
static_assert(offsetof(InitData, buffers) == 104 && offsetof(InitData, language) == 112);
constexpr int kNotLoaded = static_cast<int>(0x805a1001u), kBusy = static_cast<int>(0x80020010u);
constexpr int kInvalid = static_cast<int>(0x806a0001u);
constexpr int kFailed = static_cast<int>(0x806a0002u);

// Media Foundation is started and stopped on this owned MTA thread, never in
// DllMain, a static destructor, or an MF work-queue callback. The same thread
// will own decoder commands when source playback is added.
class MediaRuntime {
public:
    MediaRuntime() {
        worker_ = std::thread([this] { Run(); });
        std::unique_lock lock(gate_);
        changed_.wait(lock, [&] { return ready_; });
    }
    ~MediaRuntime() { Stop(); }
    HRESULT Status() const { return result_; }
    template<class Function> auto Invoke(Function function) {
        using Result = decltype(function());
        auto command = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto result = command->get_future();
        {
            std::lock_guard lock(gate_);
            if (stopping_ || !ready_ || FAILED(result_)) { throw std::runtime_error("Media worker is unavailable"); }
            commands_.emplace_back([command] { (*command)(); });
        }
        changed_.notify_all();
        return result.get();
    }
    void Stop() noexcept {
        { std::lock_guard lock(gate_); stopping_ = true; }
        changed_.notify_all();
        if (worker_.joinable()) { worker_.join(); }
    }
private:
    void Run() noexcept {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto result = com;
        bool started{};
        if (SUCCEEDED(result)) { result = MFStartup(MF_VERSION, MFSTARTUP_FULL); started = SUCCEEDED(result); }
        IMFAttributes* attributes{};
        if (SUCCEEDED(result)) { result = MFCreateAttributes(&attributes, 1); }
        if (attributes) { attributes->Release(); }
        {
            std::unique_lock lock(gate_);
            result_ = result; ready_ = true; changed_.notify_all();
            for (;;) {
                changed_.wait(lock, [&] { return stopping_ || !commands_.empty(); });
                if (commands_.empty() && stopping_) { break; }
                auto command = std::move(commands_.front()); commands_.pop_front();
                lock.unlock(); command(); lock.lock();
            }
        }
        if (started) {
            const auto shutdown = MFShutdown();
            std::printf("{\"event\":\"native_media_shutdown\",\"hresult\":%ld}\n", static_cast<long>(shutdown));
        }
        if (SUCCEEDED(com)) { CoUninitialize(); }
    }
    std::thread worker_;
    mutable std::mutex gate_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> commands_;
    bool ready_{}, stopping_{};
    HRESULT result_ = E_PENDING;
};

struct Control {
    std::uint64_t magic, handle;
    unsigned state, buffers;
    std::array<std::byte, 40> reserved{};
};
static_assert(sizeof(Control) == 64);
struct Player {
    Handle handle{};
    InitData options{};
    void* control{};
    bool closing{};
    unsigned operations{};
    std::unique_ptr<kt::media::PlaybackSession> decoder;
    std::vector<kt::media::Stream> streams;
    std::array<bool, 16> enabled{};
    std::uint64_t duration{};
    kt::media::PlayState playback_state = kt::media::PlayState::Ready;
    bool control_busy{}, poll_busy{}, drained{}, looping{}, error_event{};
    DWORD source_owner{}, control_owner{};
    std::array<DWORD, 2> frame_owner{};
    std::uint64_t generation{};
    struct Buffer { void* pointer{}; unsigned capacity{}; };
    std::array<std::array<Buffer, 16>, 2> buffers{};
    std::array<unsigned, 2> buffer_cursor{};
    std::array<std::uint64_t, 2> delivered{};
    std::thread controller;
    HANDLE controller_event{};
    std::atomic<bool> controller_quit{};
    DWORD controller_id{};
    std::array<unsigned, 16> events{};
    unsigned event_head{}, event_count{};
};
struct Registry {
    std::mutex gate;
    std::condition_variable changed;
    unsigned references{}, pending{};
    Handle next = 0x415600000001ull;
    std::unique_ptr<MediaRuntime> media;
    std::array<std::shared_ptr<Player>, 64> players{};
    std::array<bool, 64> reserved{};
};
Registry& State() {
    // Normal unload explicitly stops the worker. Abrupt guest _Exit/crash has
    // OS cleanup, not a claimed graceful MF shutdown from process teardown.
    static auto* state = new Registry;
    return *state;
}
bool Read(const void* source, void* output, std::size_t bytes) {
    SIZE_T read{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, output, bytes, &read) && read == bytes;
}
bool Writable(void* pointer, std::size_t bytes) {
    return pointer && NativeMemoryWritable(pointer, bytes);
}
bool Executable(auto callback) {
    return callback && NativeExecutableAddress(reinterpret_cast<std::uintptr_t>(callback));
}
struct Pending {
    Registry* state{};
    std::size_t index{};
    ~Pending() {
        if (state) { std::lock_guard lock(state->gate); state->reserved[index] = false; --state->pending; }
    }
};
struct Allocation {
    Allocator allocator{};
    void* pointer{};
    ~Allocation() { if (pointer) { allocator.release(allocator.object, pointer); } }
};
void QueueEvent(Player& player, unsigned event) {
    if (player.event_count == player.events.size()) { UnsupportedApi("native_avplayer_event_queue_bound", event, nullptr); }
    player.events[(player.event_head + player.event_count) % player.events.size()] = event; ++player.event_count;
    if (player.controller_event) { SetEvent(player.controller_event); }
}
void Controller(const std::shared_ptr<Player>& player) {
    auto& state = State();
    { std::lock_guard lock(state.gate); player->controller_id = GetCurrentThreadId(); }
    while (!player->controller_quit.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(player->controller_event, 10) == WAIT_FAILED) { UnsupportedApi("native_avplayer_controller_wait", GetLastError(), nullptr); }
        unsigned event{}; bool poll{}; std::uint64_t generation{};
        {
            std::lock_guard lock(state.gate);
            if (player->closing || player->controller_quit.load(std::memory_order_acquire)) { break; }
            if (player->event_count) {
                event = player->events[player->event_head]; player->event_head = (player->event_head + 1) % player->events.size(); --player->event_count;
            } else if (player->error_event) { player->error_event = false; event = 1; }
            else if (player->drained && player->playback_state == kt::media::PlayState::Playing && !player->control_busy &&
                     !player->frame_owner[0] && !player->frame_owner[1]) { player->poll_busy = true; poll = true; generation = player->generation; }
            if (event || poll) { ++player->operations; }
        }
        if (poll) {
            try {
                const auto transition = state.media->Invoke([&] { return player->decoder->Poll(); });
                std::lock_guard lock(state.gate);
                if (!player->closing && generation == player->generation) {
                    if (transition == kt::media::PlayTransition::Ended) {
                        player->playback_state = kt::media::PlayState::Ended; player->drained = false; QueueEvent(*player, 1);
                        std::printf("{\"event\":\"native_avplayer_natural_end\",\"handle\":%llu,\"video_frames\":%llu,\"audio_packets\":%llu}\n", player->handle, player->delivered[0], player->delivered[1]);
                    } else if (transition == kt::media::PlayTransition::Looped) {
                        ++player->generation; player->drained = false; QueueEvent(*player, 3);
                        std::printf("{\"event\":\"native_avplayer_loop\",\"handle\":%llu,\"generation\":%llu}\n", player->handle, player->generation);
                    }
                }
            } catch (...) {
                std::lock_guard lock(state.gate); player->playback_state = kt::media::PlayState::Failed; player->error_event = true; player->drained = false;
                std::printf("{\"event\":\"native_avplayer_controller_failure\",\"handle\":%llu}\n", player->handle);
            }
            { std::lock_guard lock(state.gate); player->poll_busy = false; --player->operations; state.changed.notify_all(); }
        }
        if (event) {
            std::printf("{\"event\":\"native_avplayer_event\",\"handle\":%llu,\"id\":%u,\"thread\":%lu}\n", player->handle, event, GetCurrentThreadId());
            try { if (player->options.event.callback) { player->options.event.callback(player->options.event.object, event, 0, nullptr); } }
            catch (...) { UnsupportedApi("native_avplayer_event_callback_exception", event, nullptr); }
            { std::lock_guard lock(state.gate); --player->operations; state.changed.notify_all(); }
        }
    }
}
void EnsureController(const std::shared_ptr<Player>& player) {
    if (player->controller.joinable()) { return; }
    player->controller_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!player->controller_event) { throw std::runtime_error("Media controller event unavailable"); }
    try { player->controller = std::thread([player] { Controller(player); }); }
    catch (...) { CloseHandle(player->controller_event); player->controller_event = nullptr; throw; }
}
__declspec(noinline) Handle Initialize(const InitData* supplied, const void* caller) {
    try {
        InitData options{};
        if (!Read(supplied, &options, sizeof(options)) || !Executable(options.memory.allocate) ||
            !Executable(options.memory.release) || !Executable(options.memory.texture) || !Executable(options.memory.release_texture) ||
            (options.event.callback && !Executable(options.event.callback)) || options.buffers < 1 || options.buffers > 16 ||
            options.auto_start > 1 || options.debug_level > 3) { return 0; }
        if (options.file.open || options.file.close || options.file.read || options.file.size) {
            UnsupportedApi("sceAvPlayerInit_custom_file_provider", 0, caller);
        }
        if (options.language) {
            bool terminated{};
            for (std::size_t index = 0; index < 16; ++index) {
                char character{};
                const auto address = reinterpret_cast<std::uintptr_t>(options.language);
                if (address > UINTPTR_MAX - index || !Read(reinterpret_cast<void*>(address + index), &character, 1)) { return 0; }
                if (!character) { terminated = true; break; }
            }
            if (!terminated) { return 0; }
        }
        auto player = std::make_shared<Player>();
        player->options = options;
        auto& state = State();
        Pending pending;
        {
            std::lock_guard lock(state.gate);
            if (!state.references || !state.media || FAILED(state.media->Status())) { return 0; }
            std::size_t index{};
            while (index < state.players.size() && (state.players[index] || state.reserved[index])) { ++index; }
            if (index == state.players.size() || state.next >= 0x415700000000ull) { return 0; }
            player->handle = state.next++;
            state.reserved[index] = true; ++state.pending; pending.state = &state; pending.index = index;
        }
        Allocation allocation{options.memory, options.memory.allocate(options.memory.object, 64, sizeof(Control))};
        if (!allocation.pointer || reinterpret_cast<std::uintptr_t>(allocation.pointer) % 64 || !Writable(allocation.pointer, sizeof(Control))) { return 0; }
        const Control control{0x4b54415649444c45ull, player->handle, 0, static_cast<unsigned>(options.buffers), {}};
        if (!CopyToNativeMemory(allocation.pointer, &control, sizeof(control))) { return 0; }
        player->control = allocation.pointer;
        {
            std::lock_guard lock(state.gate);
            state.players[pending.index] = player;
            allocation.pointer = nullptr;
        }
        std::printf("{\"event\":\"native_avplayer_init\",\"handle\":%llu,\"buffers\":%d,\"auto_start\":%s,\"source_count\":0,\"state\":\"idle\"}\n",
            static_cast<unsigned long long>(player->handle), options.buffers, options.auto_start ? "true" : "false");
        return player->handle;
    } catch (...) { return 0; }
}
Handle KT_SYSV Init(const InitData* options) { return Initialize(options, __builtin_return_address(0)); }
__declspec(noinline) int ClosePlayer(Handle handle) {
    try {
        auto& state = State();
        std::shared_ptr<Player> player;
        std::size_t index{};
        std::unique_lock lock(state.gate);
        while (index < state.players.size() && (!state.players[index] || state.players[index]->handle != handle)) { ++index; }
        if (index == state.players.size() || state.players[index]->closing) { return kInvalid; }
        player = state.players[index];
        const auto self = GetCurrentThreadId();
        if (player->operations && (self == player->source_owner || self == player->control_owner || self == player->frame_owner[0] ||
            self == player->frame_owner[1] || self == player->controller_id)) { return kFailed; }
        player->closing = true; player->event_count = 0; player->controller_quit.store(true, std::memory_order_release);
        if (player->controller_event) { SetEvent(player->controller_event); }
        state.changed.notify_all();
        state.changed.wait(lock, [&] { return !player->operations; });
        lock.unlock();
        if (player->controller.joinable()) { player->controller.join(); }
        if (player->controller_event) { CloseHandle(player->controller_event); player->controller_event = nullptr; }
        if (player->decoder) { state.media->Invoke([&] { player->decoder.reset(); }); }
        for (unsigned kind = 0; kind < 2; ++kind) { for (auto& buffer : player->buffers[kind]) {
            if (buffer.pointer) { (kind == 0 ? player->options.memory.release_texture : player->options.memory.release)(player->options.memory.object, buffer.pointer); buffer = {}; }
        } }
        player->options.memory.release(player->options.memory.object, player->control);
        { std::lock_guard finish(state.gate); state.players[index].reset(); }
        std::printf("{\"event\":\"native_avplayer_close\",\"handle\":%llu,\"source_count\":%u}\n", handle, player->streams.empty() ? 0u : 1u);
        return 0;
    } catch (...) {
        UnsupportedApi("native_avplayer_close_failure", handle, nullptr);
    }
}
int KT_SYSV Close(Handle handle) { return ClosePlayer(handle); }
__declspec(noinline) int EmptyStreamCount(Handle handle) {
    auto& state = State();
    std::lock_guard lock(state.gate);
    for (const auto& player : state.players) {
        if (player && player->handle == handle && !player->closing) { return static_cast<int>(player->streams.size()); }
    }
    return kInvalid;
}
int KT_SYSV StreamCount(Handle handle) { return EmptyStreamCount(handle); }
__declspec(noinline) bool Active(Handle handle) {
    auto& state = State(); std::lock_guard lock(state.gate);
    for (const auto& player : state.players) { if (player && player->handle == handle && !player->closing) {
        return player->playback_state == kt::media::PlayState::Playing || player->playback_state == kt::media::PlayState::Paused;
    } }
    return false;
}
bool KT_SYSV IsActive(Handle handle) { return Active(handle); }

std::shared_ptr<Player> FindLocked(Registry& state, Handle handle) {
    for (const auto& player : state.players) { if (player && player->handle == handle && !player->closing) { return player; } }
    return {};
}
struct SourceOperation {
    std::shared_ptr<Player> player;
    bool keep{};
    ~SourceOperation() {
        auto& state = State();
        if (!keep) {
            try { state.media->Invoke([&] { player->decoder.reset(); }); }
            catch (...) { UnsupportedApi("native_avplayer_source_rollback", player->handle, nullptr); }
        }
        std::lock_guard lock(state.gate); --player->operations; player->source_owner = 0; state.changed.notify_all();
    }
};
enum class PlaybackAction { Start, Pause, Resume, Stop };
int ChangePlayback(Handle handle, PlaybackAction action, const void* caller);
__declspec(noinline) int LoadSource(Handle handle, const char* supplied, const void* caller) {
    try {
        std::string path;
        const auto address = reinterpret_cast<std::uintptr_t>(supplied);
        for (std::size_t index = 0; index < 1024; ++index) {
            char character{};
            if (!address || address > UINTPTR_MAX - index || !Read(reinterpret_cast<void*>(address + index), &character, 1)) { return kInvalid; }
            if (!character) { break; }
            if (static_cast<unsigned char>(character) < 32) { return kInvalid; }
            path.push_back(character);
        }
        if (path.empty() || path.size() == 1024) { return kInvalid; }
        if (path.front() != '/') { path.insert(0, "/app0/"); }
        if (!path.starts_with("/app0/")) { return kInvalid; }
        auto& state = State();
        std::shared_ptr<Player> player;
        {
            std::lock_guard lock(state.gate); player = FindLocked(state, handle);
            if (!player) { return kInvalid; }
            if (player->operations || !player->streams.empty()) { return kFailed; }
            ++player->operations; player->source_owner = GetCurrentThreadId();
        }
        SourceOperation operation{player};
        std::fputs("[media source] ", stderr); std::fwrite(path.data(), 1, path.size(), stderr); std::fputc('\n', stderr);
        auto file = std::shared_ptr<kt::content::ReadOnlyFile>(OpenNativeContentFile(path));
        if (!file) {
            std::printf("{\"event\":\"native_avplayer_source_failure\",\"reason\":\"content_not_found\"}\n"); return kFailed;
        }
        struct SourceInfo { std::vector<kt::media::Stream> streams; std::uint64_t duration; };
        auto info = state.media->Invoke([&] {
            auto decoder = std::make_unique<kt::media::PlaybackSession>(file);
            decoder->SetLooping(player->looping);
            SourceInfo result{decoder->Streams(), decoder->Duration()};
            player->decoder = std::move(decoder); return result;
        });
        const Control control{0x4b54415649444c45ull, handle, 2, static_cast<unsigned>(player->options.buffers), {}};
        if (!CopyToNativeMemory(player->control, &control, sizeof(control))) { return kFailed; }
        {
            std::lock_guard lock(state.gate); player->streams = std::move(info.streams); player->duration = info.duration; operation.keep = true;
        }
        std::printf("{\"event\":\"native_avplayer_source_ready\",\"handle\":%llu,\"streams\":%zu,\"duration_ms\":%llu,\"file_bytes\":%llu,\"provider\":\"media_foundation\"}\n",
            handle, player->streams.size(), player->duration / 10000, file->Size());
        // Guest callbacks run here, after the worker and registry locks have
        // been released. Ready may synchronously query/enable/start streams.
        if (player->options.event.callback) {
            try { player->options.event.callback(player->options.event.object, 2, 0, nullptr); }
            catch (...) { UnsupportedApi("native_avplayer_ready_callback_exception", handle, caller); }
        }
        if (player->options.auto_start && !Active(handle)) {
            for (const auto& stream : player->streams) {
                if (!state.media->Invoke([&] { return player->decoder->Enable(stream.index); })) { return kFailed; }
            }
            { std::lock_guard lock(state.gate); for (unsigned index = 0; index < player->streams.size(); ++index) { player->enabled[index] = true; } }
            return ChangePlayback(handle, PlaybackAction::Start, caller);
        }
        return 0;
    } catch (const kt::media::Failure& error) {
        std::printf("{\"event\":\"native_avplayer_source_failure\",\"hresult\":%ld,\"operation\":\"%s\"}\n", error.hresult, error.what()); return kFailed;
    } catch (...) {
        std::printf("{\"event\":\"native_avplayer_source_failure\",\"reason\":\"host_or_content_failure\"}\n"); return kFailed;
    }
}
int KT_SYSV AddSource(Handle handle, const char* path) { return LoadSource(handle, path, __builtin_return_address(0)); }
__declspec(noinline) int StreamInfo(Handle handle, unsigned index, void* output) {
    auto& state = State();
    std::lock_guard lock(state.gate);
    const auto player = FindLocked(state, handle);
    if (!player || index >= player->streams.size() || !Writable(output, 40)) { return kInvalid; }
    const auto& stream = player->streams[index];
    std::array<std::byte, 40> info{};
    const unsigned type = stream.kind == kt::media::Kind::Video ? 0 : 1;
    std::memcpy(info.data(), &type, 4);
    if (stream.kind == kt::media::Kind::Video) {
        std::memcpy(info.data() + 8, &stream.width, 4); std::memcpy(info.data() + 12, &stream.height, 4);
        const auto aspect = static_cast<float>(stream.width) / stream.height; std::memcpy(info.data() + 16, &aspect, 4);
    } else {
        const auto channels = static_cast<std::uint16_t>(stream.channels);
        std::memcpy(info.data() + 8, &channels, 2); std::memcpy(info.data() + 12, &stream.sample_rate, 4);
    }
    const auto duration = player->duration / 10000;
    std::memcpy(info.data() + 24, &duration, 8);
    std::memcpy(output, info.data(), info.size()); return 0;
}
int KT_SYSV GetStreamInfo(Handle handle, unsigned index, void* output) { return StreamInfo(handle, index, output); }
__declspec(noinline) int SelectStream(Handle handle, unsigned index) {
    try {
        auto& state = State(); std::shared_ptr<Player> player;
        { std::lock_guard lock(state.gate); player = FindLocked(state, handle);
          if (!player || index >= player->streams.size()) { return kInvalid; } ++player->operations; }
        struct Finish { std::shared_ptr<Player> player; ~Finish() { auto& state = State(); std::lock_guard lock(state.gate); --player->operations; state.changed.notify_all(); } } finish{player};
        const auto selected = state.media->Invoke([&] { return player->decoder->Enable(player->streams[index].index); });
        if (!selected) { return kFailed; }
        { std::lock_guard lock(state.gate); player->enabled[index] = true; }
        return 0;
    } catch (...) { return kFailed; }
}
int KT_SYSV EnableStream(Handle handle, unsigned index) { return SelectStream(handle, index); }

struct ControlOperation {
    std::shared_ptr<Player> player;
    explicit ControlOperation(std::shared_ptr<Player> value) : player(std::move(value)) {}
    ControlOperation(const ControlOperation&) = delete;
    ~ControlOperation() { auto& state = State(); std::lock_guard lock(state.gate); player->control_busy = false; player->control_owner = 0; --player->operations; state.changed.notify_all(); }
};
__declspec(noinline) int ChangePlayback(Handle handle, PlaybackAction action, const void* caller) {
    std::shared_ptr<Player> player;
    std::optional<ControlOperation> operation;
    try {
        auto& state = State(); std::unique_lock lock(state.gate); player = FindLocked(state, handle);
        if (!player || player->streams.empty()) { return kFailed; }
        const auto self = GetCurrentThreadId();
        if (self == player->frame_owner[0] || self == player->frame_owner[1]) { return kFailed; }
        while ((player->control_busy || player->poll_busy) && !player->closing) {
            if (self == player->control_owner) { return kFailed; }
            state.changed.wait(lock);
        }
        if (player->closing) { return kInvalid; }
        const auto previous = player->playback_state;
        player->control_busy = true; player->control_owner = self; ++player->operations;
        if (action == PlaybackAction::Start || action == PlaybackAction::Stop) { ++player->generation; }
        operation.emplace(player);
        lock.unlock();
        if (action == PlaybackAction::Start) { EnsureController(player); }
        struct Result { bool okay; kt::media::PlayState state; bool drained; };
        const auto result = state.media->Invoke([&] {
            bool okay{};
            switch (action) {
            case PlaybackAction::Start: okay = player->decoder->Start(); break;
            case PlaybackAction::Pause: okay = player->decoder->Pause(); break;
            case PlaybackAction::Resume: okay = player->decoder->Resume(); break;
            case PlaybackAction::Stop: okay = player->decoder->Stop(); break;
            }
            return Result{okay, player->decoder->State(), player->decoder->Drained()};
        });
        lock.lock();
        if (!result.okay || player->closing) { return kFailed; }
        state.changed.wait(lock, [&] { return !player->frame_owner[0] && !player->frame_owner[1]; });
        player->playback_state = result.state; player->drained = result.drained;
        const unsigned value = result.state == kt::media::PlayState::Playing ? 3 : result.state == kt::media::PlayState::Paused ? 4 : 1;
        if (!CopyToNativeMemory(static_cast<std::byte*>(player->control) + 16, &value, 4)) {
            lock.unlock(); UnsupportedApi("native_avplayer_control_storage", handle, caller);
        }
        if (action == PlaybackAction::Start || action == PlaybackAction::Resume) { QueueEvent(*player, 3); }
        else if (action == PlaybackAction::Pause) { QueueEvent(*player, 4); }
        else if (previous == kt::media::PlayState::Playing || previous == kt::media::PlayState::Paused) { QueueEvent(*player, 1); }
        std::printf("{\"event\":\"native_avplayer_playback\",\"handle\":%llu,\"state\":%u,\"generation\":%llu}\n", handle, value, player->generation);
        lock.unlock(); return 0;
    } catch (...) {
        if (player && operation) { auto& state = State(); std::lock_guard lock(state.gate); player->playback_state = kt::media::PlayState::Failed; player->drained = false; player->error_event = true; if (player->controller_event) { SetEvent(player->controller_event); } }
        std::printf("{\"event\":\"native_avplayer_playback_failure\",\"handle\":%llu}\n", handle); return kFailed;
    }
}
int KT_SYSV Start(Handle handle) { return ChangePlayback(handle, PlaybackAction::Start, __builtin_return_address(0)); }
int KT_SYSV Pause(Handle handle) { return ChangePlayback(handle, PlaybackAction::Pause, __builtin_return_address(0)); }
int KT_SYSV Resume(Handle handle) { return ChangePlayback(handle, PlaybackAction::Resume, __builtin_return_address(0)); }
int KT_SYSV Stop(Handle handle) { return ChangePlayback(handle, PlaybackAction::Stop, __builtin_return_address(0)); }
__declspec(noinline) int Looping(Handle handle, bool looping) {
    try {
        auto& state = State(); std::shared_ptr<Player> player; bool source{};
        { std::unique_lock lock(state.gate); player = FindLocked(state, handle); if (!player) { return kInvalid; }
          while (player->source_owner && player->streams.empty() && !player->closing) { state.changed.wait(lock); }
          if (player->closing) { return kInvalid; }
          player->looping = looping; source = !player->streams.empty(); ++player->operations; }
        struct Finish { std::shared_ptr<Player> player; ~Finish() { auto& state = State(); std::lock_guard lock(state.gate); --player->operations; state.changed.notify_all(); } } finish{player};
        if (source) { state.media->Invoke([&] { player->decoder->SetLooping(looping); }); }
        return 0;
    } catch (...) { return kFailed; }
}
int KT_SYSV SetLooping(Handle handle, bool looping) { return Looping(handle, looping); }

struct FrameOperation {
    std::shared_ptr<Player> player; unsigned kind{};
    FrameOperation(std::shared_ptr<Player> value, unsigned index) : player(std::move(value)), kind(index) {}
    FrameOperation(const FrameOperation&) = delete;
    ~FrameOperation() { auto& state = State(); std::lock_guard lock(state.gate); player->frame_owner[kind] = 0; --player->operations; state.changed.notify_all(); if (player->drained && player->controller_event) { SetEvent(player->controller_event); } }
};
struct GuestAllocation {
    void* object{}; Release release{}; void* pointer{};
    ~GuestAllocation() { if (pointer) { release(object, pointer); } }
};
__declspec(noinline) bool GetFrame(Handle handle, void* output, bool video, const void* caller) {
    std::shared_ptr<Player> player;
    std::optional<FrameOperation> operation;
    const unsigned kind = video ? 0 : 1;
    try {
        const auto info_size = video ? 104u : 40u;
        if (!Writable(output, info_size)) { return false; }
        auto& state = State(); std::unique_lock lock(state.gate); player = FindLocked(state, handle);
        if (!player) { return false; }
        while ((player->control_busy || player->poll_busy) && !player->closing) {
            if (player->control_owner == GetCurrentThreadId()) { return false; }
            state.changed.wait(lock);
        }
        if (player->closing || player->playback_state != kt::media::PlayState::Playing || player->frame_owner[kind]) { return false; }
        player->frame_owner[kind] = GetCurrentThreadId(); ++player->operations;
        const auto generation = player->generation;
        operation.emplace(player, kind);
        lock.unlock();
        const auto media_kind = video ? kt::media::Kind::Video : kt::media::Kind::Audio;
        auto delivery = state.media->Invoke([&] { return player->decoder->Request(media_kind); });
        for (unsigned batches = 0; delivery.more_due; ++batches) {
            if (batches == 2048) { UnsupportedApi("native_avplayer_video_catchup_bound", handle, caller); }
            delivery = state.media->Invoke([&] { return player->decoder->Request(media_kind); });
        }
        if (!delivery.frame) {
            const auto drained = state.media->Invoke([&] { return player->decoder->Drained(); });
            std::lock_guard update(state.gate); if (generation == player->generation) { player->drained = drained; } return false;
        }
        const auto& frame = *delivery.frame;
        if (frame.bytes.empty() || frame.bytes.size() > 64 * 1024 * 1024 || frame.timestamp < 0) { UnsupportedApi("native_avplayer_frame_profile", frame.bytes.size(), caller); }
        const auto size = static_cast<unsigned>(frame.bytes.size());
        const auto count = video ? static_cast<unsigned>(player->options.buffers) : 2u;
        const auto slot = player->buffer_cursor[kind] % count;
        auto buffer = player->buffers[kind][slot];
        if (buffer.capacity < size) {
            const auto allocate = video ? player->options.memory.texture : player->options.memory.allocate;
            const auto release = video ? player->options.memory.release_texture : player->options.memory.release;
            const auto alignment = video ? 256u : 64u;
            GuestAllocation allocation{player->options.memory.object, release, allocate(player->options.memory.object, alignment, size)};
            if (!allocation.pointer || reinterpret_cast<std::uintptr_t>(allocation.pointer) % alignment || !Writable(allocation.pointer, size)) {
                std::printf("{\"event\":\"native_avplayer_frame_allocation_failure\",\"handle\":%llu,\"video\":%s,\"bytes\":%u}\n", handle, video ? "true" : "false", size); return false;
            }
            const auto base = reinterpret_cast<std::uintptr_t>(allocation.pointer);
            const auto overlaps = [&](void* pointer, unsigned bytes) { const auto other = reinterpret_cast<std::uintptr_t>(pointer); return pointer && base < other + bytes && other < base + size; };
            lock.lock();
            if (overlaps(player->control, sizeof(Control))) { lock.unlock(); UnsupportedApi("native_avplayer_allocator_alias", base, caller); }
            for (const auto& pool : player->buffers) { for (const auto& live : pool) {
                if (overlaps(live.pointer, live.capacity)) { lock.unlock(); UnsupportedApi("native_avplayer_allocator_alias", base, caller); }
            } }
            player->buffers[kind][slot] = {allocation.pointer, size}; allocation.pointer = nullptr;
            const auto replacement = player->buffers[kind][slot];
            lock.unlock();
            if (buffer.pointer) { release(player->options.memory.object, buffer.pointer); }
            buffer = replacement;
        }
        if (!CopyToNativeMemory(buffer.pointer, frame.bytes.data(), size)) { UnsupportedApi("native_avplayer_frame_storage", handle, caller); }
        struct Committed { bool okay, drained; };
        const auto committed = state.media->Invoke([&] { const auto okay = player->decoder->Commit(media_kind, delivery.token); return Committed{okay, player->decoder->Drained()}; });
        lock.lock();
        if (!committed.okay || player->closing || generation != player->generation) { lock.unlock(); return false; }
        std::array<std::byte, 104> info{};
        std::memcpy(info.data(), &buffer.pointer, 8);
        const auto timestamp = static_cast<std::uint64_t>(frame.timestamp) / 10000; std::memcpy(info.data() + 16, &timestamp, 8);
        if (video) {
            const auto& stream = frame.stream;
            std::memcpy(info.data() + 24, &stream.width, 4); std::memcpy(info.data() + 28, &stream.height, 4);
            const auto aspect = static_cast<float>(stream.width) / stream.height; std::memcpy(info.data() + 32, &aspect, 4);
            const unsigned rate = (stream.rate_numerator + stream.rate_denominator / 2) / stream.rate_denominator;
            std::memcpy(info.data() + 40, &rate, 4); std::memcpy(info.data() + 60, &stream.stride, 4);
            info[64] = std::byte{8}; info[65] = std::byte{8};
        } else {
            const auto channels = static_cast<std::uint16_t>(frame.stream.channels);
            std::memcpy(info.data() + 24, &channels, 2); std::memcpy(info.data() + 28, &frame.stream.sample_rate, 4); std::memcpy(info.data() + 32, &size, 4);
        }
        std::memcpy(output, info.data(), info_size);
        ++player->buffer_cursor[kind]; ++player->delivered[kind]; player->drained = committed.drained;
        if (player->delivered[kind] <= 32 || player->delivered[kind] % 120 == 0) {
            std::printf("{\"event\":\"native_avplayer_frame\",\"handle\":%llu,\"kind\":\"%s\",\"timestamp_ms\":%llu,\"bytes\":%u,\"slot\":%u,\"count\":%llu,\"generation\":%llu}\n",
                handle, video ? "video" : "audio", timestamp, size, slot, player->delivered[kind], generation);
        }
        lock.unlock(); return true;
    } catch (const kt::media::Failure& error) {
        std::printf("{\"event\":\"native_avplayer_frame_failure\",\"handle\":%llu,\"hresult\":%ld,\"operation\":\"%s\"}\n", handle, error.hresult, error.what());
    } catch (...) { std::printf("{\"event\":\"native_avplayer_frame_failure\",\"handle\":%llu,\"reason\":\"host_failure\"}\n", handle); }
    if (player) { auto& state = State(); std::lock_guard lock(state.gate); player->playback_state = kt::media::PlayState::Failed; player->drained = false; player->error_event = true; if (player->controller_event) { SetEvent(player->controller_event); } }
    return false;
}
bool KT_SYSV GetVideoDataEx(Handle handle, void* output) { return GetFrame(handle, output, true, __builtin_return_address(0)); }
bool KT_SYSV GetAudioData(Handle handle, void* output) { return GetFrame(handle, output, false, __builtin_return_address(0)); }
}

__declspec(noinline) int AvPlayerModule(AvPlayerModuleAction action) {
    try {
        auto& state = State();
        std::unique_lock lock(state.gate);
        if (action == AvPlayerModuleAction::IsLoaded) { return state.references ? 0 : kNotLoaded; }
        if (action == AvPlayerModuleAction::Load) {
            if (state.references == UINT32_MAX) { return kBusy; }
            if (!state.references) {
                state.media = std::make_unique<MediaRuntime>();
                const auto result = state.media->Status();
                std::printf("{\"event\":\"native_media_startup\",\"hresult\":%ld,\"provider\":\"windows_media_foundation\"}\n", static_cast<long>(result));
                if (FAILED(result)) { state.media.reset(); return kNotLoaded; }
            }
            ++state.references; return 0;
        }
        if (!state.references) { return kNotLoaded; }
        if (state.references == 1) {
            if (state.pending) { return kBusy; }
            for (const auto& player : state.players) { if (player) { return kBusy; } }
            state.media.reset();
        }
        --state.references;
        return 0;
    } catch (...) { return kNotLoaded; }
}

std::uint64_t ResolveAvPlayerImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceAvPlayer" || module != "libSceAvPlayer") { return 0; }
    identifier = identifier.substr(0, identifier.find('#'));
    struct Entry { const char* nid; std::uint64_t address; };
    const Entry entries[] = {
        {"aS66RI0gGgo", reinterpret_cast<std::uint64_t>(&Init)},
        {"NkJwDzKmIlw", reinterpret_cast<std::uint64_t>(&Close)},
        {"hdTyRzCXQeQ", reinterpret_cast<std::uint64_t>(&StreamCount)},
        {"UbQoYawOsfY", reinterpret_cast<std::uint64_t>(&IsActive)},
        {"KMcEa+rHsIo", reinterpret_cast<std::uint64_t>(&AddSource)},
        {"d8FcbzfAdQw", reinterpret_cast<std::uint64_t>(&GetStreamInfo)},
        {"ODJK2sn9w4A", reinterpret_cast<std::uint64_t>(&EnableStream)},
        {"ET4Gr-Uu07s", reinterpret_cast<std::uint64_t>(&Start)},
        {"9y5v+fGN4Wk", reinterpret_cast<std::uint64_t>(&Pause)},
        {"w5moABNwnRY", reinterpret_cast<std::uint64_t>(&Resume)},
        {"ZC17w3vB5Lo", reinterpret_cast<std::uint64_t>(&Stop)},
        {"OVths0xGfho", reinterpret_cast<std::uint64_t>(&SetLooping)},
        {"JdksQu8pNdQ", reinterpret_cast<std::uint64_t>(&GetVideoDataEx)},
        {"Wnp1OVcrZgk", reinterpret_cast<std::uint64_t>(&GetAudioData)},
    };
    for (const auto& entry : entries) { if (identifier == entry.nid) { return entry.address; } }
    return 0;
}

bool AvPlayerMediaSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch) {
    ConfigureNativeContent(app, patch, {}, app.parent_path() / "media-user");
    if (AvPlayerModule(AvPlayerModuleAction::Load)) { return false; }
    bool passed = true;
    unsigned cases{};
    const auto check = [&](const char* name, bool value) {
        ++cases; passed &= value;
        std::printf("{\"event\":\"media_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    try {
        auto source = std::shared_ptr<kt::content::ReadOnlyFile>(OpenNativeContentFile("/app0/fixture.mp4"));
        State().media->Invoke([&] {
            kt::media::Reader reader(source);
            const auto& streams = reader.Streams();
            check("readonly_mp4_has_two_real_decodable_streams_and_duration", streams.size() == 2 && reader.Duration() == 7500000);
            const auto video_found = std::find_if(streams.begin(), streams.end(), [](const auto& stream) { return stream.kind == kt::media::Kind::Video; });
            const auto audio_found = std::find_if(streams.begin(), streams.end(), [](const auto& stream) { return stream.kind == kt::media::Kind::Audio; });
            if (video_found == streams.end() || audio_found == streams.end()) { throw std::runtime_error("Expected authored audio and video streams"); }
            const auto& video = *video_found; const auto& audio = *audio_found;
            check("h264_decodes_to_declared_nv12_extent_and_rate", video.kind == kt::media::Kind::Video && video.width == 64 && video.height == 32 &&
                video.stride >= 64 && video.rate_numerator == 4 && video.rate_denominator == 1);
            check("aac_decodes_to_pcm16_stereo_48khz", audio.kind == kt::media::Kind::Audio && audio.channels == 2 && audio.sample_rate == 48000);
            unsigned video_frames{}; bool video_end{}, video_exact = true;
            for (unsigned attempt = 0; attempt < 16 && !video_end; ++attempt) {
                const auto frame = reader.Read(video.index); video_end = frame.end;
                if (frame.bytes.empty()) { continue; }
                video_exact &= frame.timestamp == static_cast<std::int64_t>(video_frames) * 2500000;
                const auto y = static_cast<int>(frame.bytes.at(0));
                const auto uv = std::size_t{frame.stream.stride} * frame.stream.height;
                video_exact &= std::abs(y - 81) <= 4 && std::abs(static_cast<int>(frame.bytes.at(uv)) - 90) <= 4 &&
                    std::abs(static_cast<int>(frame.bytes.at(uv + 1)) - 240) <= 4;
                ++video_frames;
            }
            check("three_authored_red_frames_have_real_pixels_pts_and_eof", video_frames == 3 && video_exact && video_end);
            std::uint64_t audio_frames{}; long double square_sum{};
            bool audio_end{}, audio_exact = true; std::int64_t previous = -1, first_audio_pts = INT64_MAX;
            unsigned maximum_channel_difference{};
            for (unsigned attempt = 0; attempt < 128 && !audio_end; ++attempt) {
                const auto frame = reader.Read(audio.index); audio_end = frame.end;
                if (frame.bytes.empty()) { continue; }
                if (first_audio_pts == INT64_MAX) { first_audio_pts = frame.timestamp; }
                audio_exact &= frame.timestamp >= 0 && frame.timestamp >= previous; previous = frame.timestamp;
                for (std::size_t offset = 0; offset < frame.bytes.size(); offset += 4) {
                    std::int16_t left{}, right{};
                    std::memcpy(&left, frame.bytes.data() + offset, 2); std::memcpy(&right, frame.bytes.data() + offset + 2, 2);
                    // AAC is lossy: equal source channels need not decode to
                    // bit-identical PCM. Bound the difference to 0.1% FS.
                    audio_exact &= std::abs(int{left} - int{right}) <= 32;
                    maximum_channel_difference = (std::max)(maximum_channel_difference, static_cast<unsigned>(std::abs(int{left} - int{right})));
                    square_sum += static_cast<long double>(left) * left; ++audio_frames;
                }
            }
            const auto rms = audio_frames ? std::sqrt(square_sum / audio_frames) : 0;
            check("authored_tone_has_real_pcm_monotonic_pts_bounded_extent_and_eof", audio_end && audio_exact &&
                audio_frames >= 35000 && audio_frames <= 38000 && rms > 2000 && rms < 4000);
            std::printf("{\"event\":\"media_self_test_decode\",\"video_frames\":%u,\"audio_frames\":%llu,\"audio_rms\":%.2f,\"audio_first_pts\":%lld,\"audio_eof\":%s,\"max_channel_difference\":%u}\n",
                video_frames, audio_frames, static_cast<double>(rms), first_audio_pts, audio_end ? "true" : "false", maximum_channel_difference);
            reader.Rewind();
            auto first = reader.Read(video.index);
            for (unsigned retry = 0; first.bytes.empty() && !first.end && retry < 4; ++retry) { first = reader.Read(video.index); }
            check("rewind_returns_first_frame_instead_of_fake_completion", !first.end && !first.bytes.empty() && first.timestamp == 0);
            bool invalid{};
            try { (void)reader.Read(99); } catch (const kt::media::Failure&) { invalid = true; }
            check("invalid_stream_is_an_error_not_eof", invalid);
            bool broken{};
            try { kt::media::Reader malformed(std::shared_ptr<kt::content::ReadOnlyFile>(OpenNativeContentFile("/app0/not-media.mp4"))); }
            catch (const kt::media::Failure&) { broken = true; }
            check("malformed_media_is_rejected_not_reported_as_played", broken);
            check("real_decoded_packets_follow_playback_clock_lifecycle_and_loop", kt::media::PlaybackSessionSelfTest(source));
        });
    } catch (const kt::media::Failure& error) {
        std::printf("{\"event\":\"media_self_test_failure\",\"hresult\":%ld,\"operation\":\"%s\"}\n", error.hresult, error.what()); passed = false;
    } catch (const std::exception& error) { std::fprintf(stderr, "Media self-test: %s\n", error.what()); passed = false; }
    check("media_service_unloads_after_decoders_are_destroyed", AvPlayerModule(AvPlayerModuleAction::Unload) == 0);
    std::printf("{\"event\":\"media_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false");
    return passed;
}
