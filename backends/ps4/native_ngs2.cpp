#include "native_ngs2.h"
#include "native_memory.h"
#include "memory/direct_fixture.h"
#include "native_diagnostics.h"
#include "audio/pcm_voice.h"
#include "audio/hevag.h"
#include "audio/atrac9.h"
#include "diagnostics/monotonic_clock.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <cmath>
#include <filesystem>
#include <fstream>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
using Handle = std::uint64_t;
constexpr int Error(unsigned code) { return static_cast<int>(0x804a0000u | code); }
constexpr int kFailure = Error(1), kOutAddress = Error(0x53), kOptionSize = Error(0x81), kOptionFlag = Error(0x82);
constexpr int kGrainMax = Error(0x50), kGrain = Error(0x51), kRate = Error(0x201), kAllocator = Error(0x20a);
constexpr int kAddress = Error(0x207), kAlign = Error(0x208), kSize = Error(0x209), kEmpty = Error(0x20c);
constexpr int kSystem = Error(0x230), kRackId = Error(0x260), kRack = Error(0x261), kVoiceIndex = Error(0x302);
constexpr int kOperation = Error(0x30f), kNotLoaded = static_cast<int>(0x805a1001u), kBusy = static_cast<int>(0x80020010u);
struct Context { void* buffer; std::size_t size; std::uintptr_t reserved[5]; std::uintptr_t user; };
using BufferCallback = int(KT_SYSV*)(Context*);
struct Allocator { BufferCallback allocate, release; std::uintptr_t user; };
struct SystemOption { std::size_t size; char name[16]; unsigned flags, max_grain, grain, rate, reserved[6]; };
struct RackOption { std::size_t size; char name[16]; unsigned flags, max_grain, voices, delays, matrices, ports, reserved[20]; };
struct SamplerLimits { unsigned channel_works, codec_caches, blocks, envelope_points, filters, at9_decoders, at9_channels, ajm_at9_decoders, peak_blocks; };
struct SamplerOption { RackOption rack; SamplerLimits limits; };
static_assert(sizeof(Context) == 64 && offsetof(Context, user) == 56 && sizeof(Allocator) == 24);
static_assert(sizeof(SystemOption) == 64 && offsetof(SystemOption, rate) == 36 && sizeof(RackOption) == 128);
static_assert(sizeof(SamplerOption) == 168 && offsetof(SamplerOption, limits) == 128);
struct SystemMemory { SystemOption option; std::uint64_t render_count; };
// Events are queued here; only a successful render grain may consume them.
// Play is encoded as 0 by the PS4 ABI, not the bitmask used by some PS5 APIs.
enum class VoiceEvent : unsigned { None, Play, Stop, Kill, Pause, Resume };
constexpr unsigned kInUse = 1, kPlaying = 2, kPaused = 4, kStopped = 8, kWaveEmpty = 32;
struct WaveFormat { unsigned type, channels, rate, config, offset, margin; };
// A private format capability issued by this provider's VAG parser and passed
// opaquely by the verified game caller into our sampler. Not a claimed Sony
// enum value; hard-coded guest codec types remain separately validated.
constexpr unsigned kParsedHevag = 0x4b485631;
constexpr unsigned kParsedAtrac9 = 0x4b415439; // Private provider capability, not a Sony codec enum.
struct VoiceMemory {
    unsigned state, channels; float gains[8]; std::uint64_t cursor; VoiceEvent event;
    WaveFormat format{};
    Handle destination{};
    float port_gain = 1, pitch = 1;
    unsigned setup_flags{};
    bool paused{};
    std::array<VoiceEvent, 8> events{}; unsigned event_count{};
    std::array<float, 64> matrix{}; unsigned matrix_count{};
    int port_matrix{};
    unsigned flags{}, last_reported_flags = UINT32_MAX;
};
struct alignas(8) RackMemory { unsigned id, voices, matrices, ports; SamplerLimits limits; };
struct ParamHeader { std::uint16_t size; std::int16_t next; unsigned id; };
struct MasterSetup { ParamHeader header; unsigned channels, flags; };
struct EventParam { ParamHeader header; unsigned id; };
struct SamplerSetup { ParamHeader header; WaveFormat format; unsigned flags, reserved; };
struct PatchParam { ParamHeader header; unsigned port, input; Handle destination; };
struct ValueParam { ParamHeader header; unsigned port; float value; };
struct PitchParam { ParamHeader header; float value; unsigned reserved; };
struct MatrixParam { ParamHeader header; unsigned index, count; const float* levels; };
struct PortMatrixParam { ParamHeader header; unsigned port; int matrix; };
struct WaveBlock { unsigned offset, bytes, repeats, skip, samples, reserved; std::uint64_t user; };
struct WaveInfo {
    WaveFormat format;
    unsigned offset, bytes, loop_begin, loop_end, samples;
    unsigned unit_bytes, unit_samples, units_per_frame, frame_bytes, frame_samples, delay, blocks;
    std::array<WaveBlock, 4> block{};
};
static_assert(sizeof(WaveInfo) == 200 && offsetof(WaveInfo, blocks) == 68 && offsetof(WaveInfo, block) == 72);
struct BlocksParam { ParamHeader header; const void* data; unsigned flags, count; const WaveBlock* blocks; };
struct RenderBuffer { void* buffer; std::size_t size; unsigned waveform, channels; };
static_assert(sizeof(RenderBuffer) == 24);
static_assert(sizeof(ParamHeader) == 8 && sizeof(MasterSetup) == 16 && sizeof(EventParam) == 12);
static_assert(sizeof(WaveFormat) == 24 && sizeof(SamplerSetup) == 40 && offsetof(SamplerSetup, flags) == 32);
static_assert(sizeof(PatchParam) == 24 && sizeof(WaveBlock) == 32 && sizeof(BlocksParam) == 32);
static_assert(sizeof(MatrixParam) == 24 && sizeof(PortMatrixParam) == 16);
std::atomic<std::size_t> resident_pcm_bytes{};
struct PcmBlock {
    std::vector<std::byte> data;
    unsigned repeats{}, skip{}, samples{};
    std::uint64_t user{};
    bool accounted{};
    ~PcmBlock() { if (accounted) { resident_pcm_bytes.fetch_sub(data.size(), std::memory_order_relaxed); } }
};
struct SamplerState {
    std::vector<std::shared_ptr<PcmBlock>> queued;
    std::shared_ptr<PcmBlock> active;
    kt::audio::PcmVoice pcm;
    bool complete{};
    double phase{};
    std::uint64_t submissions{};
};
struct OwnedBuffer {
    Context info{}; BufferCallback release{};
    int Free() { const auto result = release(&info); return result > 0 ? kFailure : result; }
};
struct System {
    OwnedBuffer buffer; SystemMemory* memory{};
    unsigned children = 0, pending = 0;
    unsigned last_active = UINT32_MAX;
    std::uint64_t last_render_clock_us{}, last_timing_report_us{}, starved_grains{};
};
struct Rack {
    OwnedBuffer buffer; std::shared_ptr<System> parent;
    RackMemory* memory{}; VoiceMemory* voices{}; Handle first_voice{};
    DWORD lock_owner{}; unsigned lock_depth{};
    std::uint64_t lock_began_clock_us{}, lock_id{};
    std::vector<SamplerState> samplers;
};
struct Registry {
    std::mutex mutex;
    unsigned references = 0, pending = 0;
    Handle next = 0x4e47000000000001ull;
    std::uint64_t next_lock_id{};
    std::map<Handle, std::shared_ptr<System>> systems;
    std::map<Handle, std::shared_ptr<Rack>> racks;
};
Registry registry;
// External rack locks exclude render, not ordinary VoiceControl/GetVoice.
// The game queues stream data while holding its own mutex; a rack owner may
// need that mutex before unlocking. Making Control join this lock is ABBA.
// Individual graph commands/render remain atomic under registry.mutex.
// Allocation callbacks run outside the registry lock.
std::recursive_mutex graph_access;
struct VoiceRef { std::shared_ptr<Rack> rack; VoiceMemory* memory{}; SamplerState* sampler{}; };
VoiceRef FindVoiceLocked(Handle handle) {
    for (const auto& [key, rack] : registry.racks) {
        (void)key;
        if (handle >= rack->first_voice && handle - rack->first_voice < rack->memory->voices) {
            const auto index = static_cast<std::size_t>(handle - rack->first_voice);
            return {rack, &rack->voices[index], rack->samplers.empty() ? nullptr : &rack->samplers[index]};
        }
    }
    return {};
}

int QueuePcm(const BlocksParam& request, const VoiceRef& ref) {
    if (!ref.sampler || !ref.memory->format.type) { return Error(0x301); }
    if (request.count > ref.rack->memory->limits.blocks) { return Error(0x106); }
    if (request.count && (!request.data || !request.blocks)) { return Error(0x154); }
    const auto descriptions = reinterpret_cast<std::uintptr_t>(request.blocks);
    if (descriptions > UINTPTR_MAX - std::size_t{request.count} * sizeof(WaveBlock)) { return Error(0x154); }
    if (request.count + ref.sampler->queued.size() + (ref.sampler->active ? 1 : 0) > ref.rack->memory->limits.blocks) { return kEmpty; }
    auto queue = ref.sampler->queued;
    const bool hevag = ref.memory->format.type == kParsedHevag;
    const bool atrac9 = ref.memory->format.type == kParsedAtrac9;
    const auto at9_codec = atrac9 ? kt::audio::InspectAtrac9Config(ref.memory->format.config) : kt::audio::Atrac9Config{};
    const auto stride = ref.memory->channels * (hevag ? 16u : ref.memory->format.type == 18 ? 2u : 4u);
    for (unsigned index = 0; index < request.count; ++index) {
        WaveBlock description{}; SIZE_T read{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(descriptions + index * sizeof(WaveBlock)), &description, sizeof(description), &read) || read != sizeof(description)) { return Error(0x154); }
        const auto decoded_samples = atrac9 ? static_cast<std::uint64_t>(description.bytes / at9_codec.superframe_bytes) * at9_codec.SuperframeSamples() :
            static_cast<std::uint64_t>(description.bytes / stride) * (hevag ? 28u : 1u);
        // The original stream producer at eboot +0x11d984 writes the real
        // fields but leaves the word at +20 uninitialized. It has no meaning
        // in this profile; requiring zero rejects otherwise valid PCM blocks.
        if (description.bytes % (atrac9 ? at9_codec.superframe_bytes : stride) || description.skip > decoded_samples) { return Error(0x106); }
        const auto available = decoded_samples - description.skip;
        if (available > UINT32_MAX) { return Error(0x106); }
        const auto samples = description.samples ? description.samples : static_cast<unsigned>(available);
        if (samples > available || (!samples && description.repeats)) { return Error(0x106); }
        if (description.bytes > 16u * 1024 * 1024) { return kEmpty; }
        const auto address = reinterpret_cast<std::uintptr_t>(request.data);
        if (address > UINTPTR_MAX - description.offset || address + description.offset > UINTPTR_MAX - description.bytes) { return Error(0x154); }
        auto block = std::make_shared<PcmBlock>();
        block->data.resize(description.bytes); block->repeats = description.repeats;
        block->skip = description.skip; block->samples = samples; block->user = description.user;
        if (description.bytes && (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address + description.offset),
                block->data.data(), description.bytes, &read) || read != description.bytes)) { return Error(0x154); }
        if (hevag) {
            try { block->data = kt::audio::DecodeHevag(block->data, ref.memory->channels); }
            catch (const kt::audio::VagFailure& failure) {
                std::printf("{\"event\":\"native_ngs2_hevag_decode_failure\",\"reason\":\"%s\"}\n", failure.reason);
                return Error(0x408);
            }
        }
        if (atrac9) {
            const auto began=std::chrono::steady_clock::now();
            try { block->data=kt::audio::DecodeAtrac9(block->data,ref.memory->format.config); }
            catch (const kt::audio::Atrac9Failure& failure) {
                std::printf("{\"event\":\"native_ngs2_atrac9_decode_failure\",\"reason\":\"%s\"}\n",failure.reason);
                return Error(0x408);
            }
            static std::atomic<unsigned> logged{};
            if (logged.fetch_add(1,std::memory_order_relaxed)<128) {
                const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();
                std::printf("{\"event\":\"native_ngs2_atrac9_decoded\",\"voice\":%llu,\"compressed_bytes\":%u,\"pcm_bytes\":%zu,\"skip\":%u,\"samples\":%u,\"repeats\":%u,\"decode_us\":%lld,\"clock_us\":%llu}\n",
                    ref.rack->first_voice+(ref.memory-ref.rack->voices),description.bytes,block->data.size(),block->skip,block->samples,block->repeats,micros,
                    kt::diagnostics::MonotonicMicroseconds());
            }
        }
        auto resident = resident_pcm_bytes.load(std::memory_order_relaxed);
        constexpr std::size_t maximum = 256ull * 1024 * 1024;
        do { if (block->data.size() > maximum || resident > maximum - block->data.size()) { return kEmpty; } }
        while (!resident_pcm_bytes.compare_exchange_weak(resident, resident + block->data.size(), std::memory_order_relaxed));
        block->accounted = true;
        queue.push_back(std::move(block));
    }
    ref.sampler->queued = std::move(queue); ref.sampler->complete = !(request.flags & 1);
    if (!ref.sampler->queued.empty() || ref.sampler->active) { ref.memory->flags &= ~kWaveEmpty; }
    return 0;
}

template<typename Function> __declspec(noinline) int Api(const void* caller, Function&& function) {
    try { return function(); }
    catch (const std::bad_alloc&) { return kEmpty; }
    catch (...) { UnsupportedApi("native_ngs2_host_exception", 0, caller); }
}
void TraceCommitted(const char* name, Handle handle, const void* caller) noexcept {
    try { TraceNativeApi(name, handle, caller); }
    catch (...) { std::puts("{\"event\":\"native_ngs2_trace_dropped\",\"reason\":\"host_exception\"}"); }
}
// These guards never hold the registry mutex across a game allocator callback.
// Pending transactions also prevent reentrant destruction or last-reference unload.
struct Pending {
    std::shared_ptr<System> parent; bool active = false;
    ~Pending() {
        if (active) { std::lock_guard lock(registry.mutex); --registry.pending; if (parent) { --parent->pending; } }
    }
};
struct Allocation {
    OwnedBuffer buffer; bool owned = false;
    ~Allocation() {
        if (owned && buffer.Free() != 0) { std::fputs("NGS2 allocator rollback: free callback failed\n", stderr); }
    }
};
bool Valid(const Allocator* allocator, bool fixture) {
    return allocator && allocator->allocate && allocator->release &&
        (fixture || (NativeExecutableAddress(reinterpret_cast<std::uintptr_t>(allocator->allocate)) &&
                     NativeExecutableAddress(reinterpret_cast<std::uintptr_t>(allocator->release))));
}
int Allocate(const Allocator& allocator, std::size_t bytes, Allocation& result) {
    result.buffer = {{nullptr, bytes, {}, allocator.user}, allocator.release};
    const auto status = allocator.allocate(&result.buffer.info);
    result.owned = result.buffer.info.buffer != nullptr;
    if (status != 0) { return status > 0 ? kFailure : status; }
    if (!result.owned) { return kAddress; }
    if (reinterpret_cast<std::uintptr_t>(result.buffer.info.buffer) % 8) { return kAlign; }
    if (result.buffer.info.size < bytes) { return kSize; }
    return 0;
}
int SystemCreate(const SystemOption* supplied, const Allocator* allocator, Handle* output, const void* caller, bool fixture = false, bool fail_commit = false) {
    return Api(caller, [&] {
        if (!output) { return kOutAddress; }
        if (!Valid(allocator, fixture)) { return kAllocator; }
        const auto option = supplied ? *supplied : SystemOption{sizeof(SystemOption), {}, 0, 512, 256, 48000, {}};
        if (option.size != sizeof(SystemOption)) { return kOptionSize; }
        if (std::any_of(std::begin(option.reserved), std::end(option.reserved), [](auto v) { return v != 0; })) { return kOptionFlag; }
        if (option.flags) { UnsupportedApi("sceNgs2SystemCreateWithAllocator_flags", option.flags, caller); }
        if (option.max_grain < 64 || option.max_grain > 1024 || option.max_grain % 64) { return kGrainMax; }
        if (option.grain < 64 || option.grain > option.max_grain || option.grain % 64) { return kGrain; }
        constexpr std::array rates{11025u, 12000u, 22050u, 24000u, 44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
        if (std::find(rates.begin(), rates.end(), option.rate) == rates.end()) { return kRate; }
        Pending pending;
        {
            std::lock_guard lock(registry.mutex);
            if (!registry.references) { return kFailure; }
            if (registry.systems.size() + registry.pending >= 16) { return kEmpty; }
            ++registry.pending; pending.active = true;
        }
        const auto callbacks = *allocator;
        Allocation allocation;
        // Guest-owned storage contains live system state and a stereo mixing
        // workspace, rather than a token-only or zero-length dummy allocation.
        const auto bytes = sizeof(SystemMemory) + option.max_grain * 2 * sizeof(float);
        if (const auto error = Allocate(callbacks, bytes, allocation)) { return error; }
        std::memset(allocation.buffer.info.buffer, 0, bytes);
        auto system = std::make_shared<System>();
        system->buffer = allocation.buffer;
        system->memory = ::new (system->buffer.info.buffer) SystemMemory{option, 0};
        Handle handle;
        {
            std::lock_guard lock(registry.mutex);
            if (fail_commit) { throw std::bad_alloc(); }
            if (registry.systems.size() >= 16) { return kEmpty; }
            if (registry.next == (std::numeric_limits<Handle>::max)()) { return kEmpty; }
            handle = registry.next;
            registry.systems.emplace(handle, system); ++registry.next;
            allocation.owned = false;
            *output = handle;
        }
        std::printf("{\"event\":\"native_ngs2_system\",\"operation\":\"create\",\"handle\":%llu,\"grain\":%u,\"rate\":%u,\"bytes\":%zu}\n",
                    handle, option.grain, option.rate, bytes);
        TraceCommitted("sceNgs2SystemCreateWithAllocator", handle, caller);
        return 0;
    });
}
int RackCreate(Handle owner, unsigned id, const RackOption* supplied, const Allocator* allocator, Handle* output, const void* caller, bool fixture = false) {
    return Api(caller, [&] {
        if (!output) { return kOutAddress; }
        if (!Valid(allocator, fixture)) { return kAllocator; }
        if (id != 0x3000 && id != 0x1000) {
            if (id == 0x2000) { UnsupportedApi("sceNgs2RackCreateWithAllocator_rack_profile", id, caller); }
            return kRackId;
        }
        if (id == 0x1000 && !supplied) { UnsupportedApi("sceNgs2RackCreateWithAllocator_sampler_default", 0, caller); }
        const auto option = supplied ? *supplied : RackOption{sizeof(RackOption), {}, 0, 512, 1, 0, 1, 8, {}};
        if (option.size != (id == 0x1000 ? sizeof(SamplerOption) : sizeof(RackOption))) { return kOptionSize; }
        const auto limits = id == 0x1000 ? reinterpret_cast<const SamplerOption*>(supplied)->limits : SamplerLimits{};
        if (option.flags || std::any_of(std::begin(option.reserved), std::end(option.reserved), [](auto v) { return v != 0; })) { return kOptionFlag; }
        if (!option.voices || option.voices > 256) { return Error(0x103); }
        if (option.max_grain < 64 || option.max_grain > 1024 || option.max_grain % 64) { return kGrainMax; }
        if (option.delays || option.matrices != 1 || option.ports != 8) { UnsupportedApi("sceNgs2RackCreateWithAllocator_limits", option.ports, caller); }
        if (id == 0x1000) {
            if (!limits.channel_works || limits.channel_works > 2048) { return Error(0x105); }
            if (limits.codec_caches > 2048) { return Error(0x109); }
            if (!limits.blocks || limits.blocks > 256) { return Error(0x106); }
            if (limits.envelope_points > 256) { return Error(0x107); }
            if (limits.filters > 64) { return Error(0x108); }
            if (limits.at9_decoders > 2048) { return Error(0x10a); }
            if (limits.at9_channels > 2048) { return Error(0x10b); }
            if (limits.ajm_at9_decoders > 2048 || limits.peak_blocks > 256) { UnsupportedApi("sceNgs2RackCreateWithAllocator_sampler_limits", limits.ajm_at9_decoders, caller); }
        }
        Pending pending;
        {
            std::lock_guard lock(registry.mutex);
            const auto found = registry.systems.find(owner);
            if (found == registry.systems.end()) { return kSystem; }
            if (registry.racks.size() + registry.pending >= 64) { return kEmpty; }
            pending.parent = found->second;
            if (option.max_grain > pending.parent->memory->option.max_grain) { return kGrainMax; }
            ++pending.parent->pending; ++registry.pending; pending.active = true;
        }
        const auto callbacks = *allocator;
        Allocation allocation;
        const auto bytes = sizeof(RackMemory) + sizeof(VoiceMemory) * option.voices;
        if (const auto error = Allocate(callbacks, bytes, allocation)) { return error; }
        std::memset(allocation.buffer.info.buffer, 0, bytes);
        auto rack = std::make_shared<Rack>();
        if (id == 0x1000) { rack->samplers.resize(option.voices); }
        rack->buffer = allocation.buffer; rack->parent = pending.parent;
        rack->memory = ::new (rack->buffer.info.buffer) RackMemory{id, option.voices, option.matrices, option.ports, limits};
        rack->voices = reinterpret_cast<VoiceMemory*>(static_cast<std::byte*>(rack->buffer.info.buffer) + sizeof(RackMemory));
        for (unsigned i = 0; i < option.voices; ++i) {
            auto* voice = ::new (&rack->voices[i]) VoiceMemory{};
            std::fill(std::begin(voice->gains), std::end(voice->gains), 1.0f);
        }
        Handle handle;
        {
            std::lock_guard lock(registry.mutex);
            if (registry.racks.size() >= 64 || registry.next > (std::numeric_limits<Handle>::max)() - option.voices - 1) { return kEmpty; }
            handle = registry.next; rack->first_voice = handle + 1;
            registry.racks.emplace(handle, rack); registry.next += option.voices + 1;
            ++pending.parent->children; allocation.owned = false; *output = handle;
        }
        std::printf("{\"event\":\"native_ngs2_rack\",\"operation\":\"create\",\"handle\":%llu,\"rack_id\":%u,\"voices\":%u,\"bytes\":%zu}\n",
                    handle, id, option.voices, bytes);
        if (id == 0x1000) {
            std::printf("{\"event\":\"native_ngs2_sampler_limits\",\"channel_works\":%u,\"codec_caches\":%u,\"blocks\":%u,\"envelope_points\":%u,\"filters\":%u,\"at9_decoders\":%u,\"at9_channels\":%u,\"ajm_at9_decoders\":%u,\"peak_blocks\":%u}\n",
                        limits.channel_works, limits.codec_caches, limits.blocks, limits.envelope_points, limits.filters,
                        limits.at9_decoders, limits.at9_channels, limits.ajm_at9_decoders, limits.peak_blocks);
        }
        TraceCommitted("sceNgs2RackCreateWithAllocator", handle, caller); return 0;
    });
}
int GetVoice(Handle handle, unsigned index, Handle* output) {
    std::lock_guard lock(registry.mutex);
    const auto found = registry.racks.find(handle);
    if (found == registry.racks.end()) { return kRack; }
    if (!output) { return kOutAddress; }
    if (index >= found->second->memory->voices) { return kVoiceIndex; }
    *output = found->second->first_voice + index; return 0;
}
int Control(Handle handle, const ParamHeader* params, const void* caller) {
    return Api(caller, [&] {
        if (!params) { return Error(0x309); }
        const auto header = *params;
        if (header.size < sizeof(ParamHeader)) { return Error(0x30a); }
        std::unique_lock lock(registry.mutex);
        const auto ref = FindVoiceLocked(handle);
        auto* voice = ref.memory;
        const auto rack_id = ref.rack ? ref.rack->memory->id : 0;
        if (!voice) { return Error(0x300); }
        if (header.next) {
            lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_linked_commands", static_cast<std::uint16_t>(header.next), caller);
        }
        if (header.id == 6) {
            if (header.size != sizeof(EventParam)) { return Error(0x30a); }
            const auto event = *reinterpret_cast<const EventParam*>(params);
            if (event.id > 5) { return Error(0x303); }
            if ((event.id == 0 || event.id == 4 || event.id == 5) && !voice->channels) { return Error(0x301); }
            if (voice->event_count == voice->events.size()) { return kEmpty; }
            constexpr std::array values{VoiceEvent::Play, VoiceEvent::Stop, VoiceEvent::Stop, VoiceEvent::Kill, VoiceEvent::Pause, VoiceEvent::Resume};
            voice->event = values[event.id]; voice->events[voice->event_count++] = voice->event;
            // Data commands are accepted in call order. Clearing here keeps
            // Stop -> new blocks -> Play from erasing the NEW blocks at render.
            if (ref.sampler && (voice->event == VoiceEvent::Stop || voice->event == VoiceEvent::Kill)) { *ref.sampler = {}; }
            return 0;
        }
        if (header.id == 0x10000000 && ref.sampler) {
            if (header.size != sizeof(SamplerSetup)) { return Error(0x30a); }
            const auto setup = *reinterpret_cast<const SamplerSetup*>(params);
            if (!setup.format.channels || setup.format.channels > 8) { return Error(0x52); }
            if (!setup.format.rate) { return kRate; }
            if (setup.reserved) { return kOptionFlag; }
            const bool atrac9=setup.format.type==kParsedAtrac9;
            if (atrac9) {
                try {
                    const auto codec=kt::audio::InspectAtrac9Config(setup.format.config);
                    if (codec.channels!=setup.format.channels || codec.rate!=setup.format.rate) { return Error(0x408); }
                } catch (const kt::audio::Atrac9Failure&) { return Error(0x408); }
            }
            if ((setup.format.type != 18 && setup.format.type != 24 && setup.format.type != kParsedHevag && !atrac9) || setup.format.channels > 2 ||
                setup.format.rate < 8000 || setup.format.rate > 192000 || (!atrac9 && setup.format.config) || setup.format.offset || setup.format.margin || setup.flags > 1) {
                lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_sampler_format_profile", setup.format.type, caller);
            }
            *ref.sampler = {};
            voice->format = setup.format; voice->setup_flags = setup.flags; voice->channels = setup.format.channels;
            voice->state = 0; voice->cursor = 0; voice->paused = false; voice->pitch = 1;
            voice->flags = kInUse | kWaveEmpty;
            voice->event = VoiceEvent::None; voice->event_count = 0;
            std::printf("{\"event\":\"native_ngs2_sampler_setup\",\"voice\":%llu,\"waveform\":%u,\"channels\":%u,\"rate\":%u,\"flags\":%u}\n",
                        handle, setup.format.type, setup.format.channels, setup.format.rate, setup.flags);
            return 0;
        }
        if (header.id == 5) {
            if (header.size != sizeof(PatchParam)) { return Error(0x30a); }
            const auto patch = *reinterpret_cast<const PatchParam*>(params);
            if (patch.port >= ref.rack->memory->ports) { return Error(0x304); }
            const auto destination = FindVoiceLocked(patch.destination);
            if (patch.destination && (!destination.memory || destination.rack->parent != ref.rack->parent || destination.sampler)) { return Error(0x305); }
            if (patch.port || patch.input || !ref.sampler) { lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_patch_profile", patch.port, caller); }
            voice->destination = patch.destination;
            return 0;
        }
        if (header.id == 1) {
            if (header.size != sizeof(MatrixParam)) { return Error(0x30a); }
            const auto matrix = *reinterpret_cast<const MatrixParam*>(params);
            if (matrix.index >= ref.rack->memory->matrices) { return Error(0x700); }
            if (matrix.count > voice->matrix.size()) { return Error(0x151); }
            if (matrix.count && !matrix.levels) { return Error(0x153); }
            if (matrix.index) { lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_matrix_profile", matrix.index, caller); }
            std::array<float, 64> values{}; SIZE_T read{};
            const auto bytes = std::size_t{matrix.count} * sizeof(float);
            if (bytes && (!ReadProcessMemory(GetCurrentProcess(), matrix.levels, values.data(), bytes, &read) || read != bytes)) { return Error(0x153); }
            if (std::any_of(values.begin(), values.end(), [](auto value) { return !std::isfinite(value); })) { return kOperation; }
            voice->matrix = values; voice->matrix_count = matrix.count;
            return 0;
        }
        if (header.id == 3) {
            if (header.size != sizeof(PortMatrixParam)) { return Error(0x30a); }
            const auto value = *reinterpret_cast<const PortMatrixParam*>(params);
            if (value.port >= ref.rack->memory->ports) { return Error(0x304); }
            if (value.matrix < -1 || value.matrix >= static_cast<int>(ref.rack->memory->matrices)) { return Error(0x700); }
            if (value.port) { lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_port_matrix_profile", value.port, caller); }
            voice->port_matrix = value.matrix; return 0;
        }
        if (header.id == 0x10000001 && ref.sampler) {
            if (header.size != sizeof(BlocksParam)) { return Error(0x30a); }
            const auto request = *reinterpret_cast<const BlocksParam*>(params);
            if (request.flags & ~1u) { lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_waveform_flags", request.flags, caller); }
            const auto result = QueuePcm(request, ref);
            const auto submission = ++ref.sampler->submissions;
            if (result || submission <= 4 || submission % 1024 == 0) {
                std::printf("{\"event\":\"native_ngs2_pcm_queue\",\"voice\":%llu,\"submission\":%llu,\"blocks\":%u,\"result\":%d,\"queued\":%zu,\"resident_bytes\":%zu}\n",
                            handle, submission, request.count, result, ref.sampler->queued.size(), resident_pcm_bytes.load());
            }
            return result;
        }
        if (header.id == 2) {
            if (header.size != sizeof(ValueParam)) { return Error(0x30a); }
            const auto value = *reinterpret_cast<const ValueParam*>(params);
            if (value.port >= ref.rack->memory->ports) { return Error(0x304); }
            if (!std::isfinite(value.value)) { return kOperation; }
            if (value.port) { lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_port_volume_profile", value.port, caller); }
            voice->port_gain = value.value; return 0;
        }
        if (header.id == 0x10000005 && ref.sampler) {
            if (header.size != sizeof(PitchParam)) { return Error(0x30a); }
            const auto value = *reinterpret_cast<const PitchParam*>(params);
            if (value.reserved || !std::isfinite(value.value) || value.value <= 0 || value.value > 16) { return kOperation; }
            voice->pitch = value.value; return 0;
        }
        if (header.id == 0x30000000 && rack_id == 0x3000) {
            if (header.size != sizeof(MasterSetup)) { return Error(0x30a); }
            const auto setup = *reinterpret_cast<const MasterSetup*>(params);
            if (setup.channels < 1 || setup.channels > 8) { return Error(0x52); }
            if (setup.flags) {
                lock.unlock(); UnsupportedApi("sceNgs2VoiceControl_master_profile", (static_cast<Handle>(setup.flags) << 32) | setup.channels, caller);
            }
            voice->channels = setup.channels;
            voice->flags = kInUse | (voice->state ? kPlaying : 0) | (voice->paused ? kPaused : 0);
            return 0;
        }
        lock.unlock();
        std::array<unsigned, 32> payload{}; SIZE_T captured{};
        (void)ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const std::byte*>(params) + 8, payload.data(),
                               std::min<std::size_t>(sizeof(payload), header.size - 8), &captured);
        char words[32 * 11 + 3]{}; auto* cursor = words; *cursor++ = '[';
        for (std::size_t index = 0; index < captured / 4; ++index) { cursor += std::sprintf(cursor, "%s%u", index ? "," : "", payload[index]); }
        *cursor++ = ']'; *cursor = 0;
        std::printf("{\"event\":\"native_ngs2_unknown_control\",\"voice\":%llu,\"rack_id\":%u,\"id\":%u,\"size\":%u,\"next\":%d,\"payload_word0\":%u,\"payload_bytes_captured\":%zu,\"payload_words\":%s}\n",
                    handle, rack_id, header.id, header.size, header.next, payload[0], captured, words);
        UnsupportedApi("sceNgs2VoiceControl_command", header.id, caller);
    });
}
int Destroy(Handle handle, Context* output, bool is_system, const void* caller) {
    return Api(caller, [&] {
        // Destruction serializes with active commands/render on the registry.
        // A locked target is rejected below; do not wait for its owner while
        // a caller may hold another game-side mutex needed by that owner.
        // The game's cleanup requests a null context output. Ownership-return
        // variants need their own verified contract before they are enabled.
        if (output) { UnsupportedApi("sceNgs2Destroy_context_output", handle, caller); }
        OwnedBuffer buffer;
        Pending pending;
        {
            std::lock_guard lock(registry.mutex);
            if (is_system) {
                const auto found = registry.systems.find(handle);
                if (found == registry.systems.end()) { return kSystem; }
                if (found->second->pending || found->second->children) { return kOperation; }
                buffer = found->second->buffer; registry.systems.erase(found);
            } else {
                const auto found = registry.racks.find(handle);
                if (found == registry.racks.end()) { return kRack; }
                if (found->second->lock_depth) { return kOperation; }
                for (const auto& [other_handle, other] : registry.racks) {
                    if (other_handle == handle) { continue; }
                    for (unsigned index = 0; index < other->memory->voices; ++index) {
                        const auto destination = other->voices[index].destination;
                        if (destination >= found->second->first_voice && destination - found->second->first_voice < found->second->memory->voices) { return kOperation; }
                    }
                }
                buffer = found->second->buffer; pending.parent = found->second->parent;
                --pending.parent->children; ++pending.parent->pending; registry.racks.erase(found);
            }
            ++registry.pending; pending.active = true;
        }
        const auto result = buffer.Free();
        std::printf("{\"event\":\"native_ngs2_destroy\",\"handle\":%llu,\"system\":%s,\"result\":%d}\n", handle, is_system ? "true" : "false", result);
        return result;
    });
}
int KT_SYSV CreateSystem(const SystemOption* option, const Allocator* allocator, Handle* output) {
    return SystemCreate(option, allocator, output, __builtin_return_address(0));
}
int KT_SYSV CreateRack(Handle system, unsigned id, const RackOption* option, const Allocator* allocator, Handle* output) {
    return RackCreate(system, id, option, allocator, output, __builtin_return_address(0));
}
int KT_SYSV RackGetVoice(Handle rack, unsigned index, Handle* output) {
    return Api(__builtin_return_address(0), [&] { return GetVoice(rack, index, output); });
}
int GetFlags(Handle handle, unsigned* output, const void* caller) {
    return Api(caller, [&] {
        // Published flags are protected by the registry, not by the external
        // rack-control lock. A caller can hold its own voice mutex here while
        // the rack owner needs that mutex; joining graph_access makes an ABBA
        // cycle. Writers and destruction already serialize on registry.mutex.
        std::lock_guard lock(registry.mutex);
        const auto ref = FindVoiceLocked(handle);
        if (!ref.memory) { return Error(0x300); }
        if (!output) { return kOutAddress; }
        *output = ref.memory->flags;
        if (ref.memory->last_reported_flags != ref.memory->flags) {
            std::printf("{\"event\":\"native_ngs2_state_flags\",\"voice\":%llu,\"flags\":%u}\n", handle, ref.memory->flags);
            ref.memory->last_reported_flags = ref.memory->flags;
        }
        return 0;
    });
}
int KT_SYSV VoiceGetStateFlags(Handle voice, unsigned* output) { return GetFlags(voice, output, __builtin_return_address(0)); }
int KT_SYSV VoiceControl(Handle voice, const ParamHeader* params) { return Control(voice, params, __builtin_return_address(0)); }
int LockRack(Handle handle, const void* caller) {
    return Api(caller, [&] {
        const auto began = kt::diagnostics::MonotonicMicroseconds();
        std::unique_lock graph(graph_access);
        const auto acquired = kt::diagnostics::MonotonicMicroseconds();
        unsigned depth{};
        std::uint64_t lock_id{}, lock_began{};
        {
            std::lock_guard lock(registry.mutex);
            const auto found = registry.racks.find(handle);
            if (found == registry.racks.end()) { return kRack; }
            auto& rack = *found->second;
            if (rack.lock_depth == UINT32_MAX || (rack.lock_depth && rack.lock_owner != GetCurrentThreadId())) { return kOperation; }
            if (!rack.lock_depth) { rack.lock_began_clock_us = acquired; rack.lock_id = ++registry.next_lock_id; }
            rack.lock_owner = GetCurrentThreadId(); ++rack.lock_depth;
            depth = rack.lock_depth;
            lock_id = rack.lock_id; lock_began = rack.lock_began_clock_us;
        }
        // Transfer this recursive acquisition to the matching explicit Unlock.
        (void)graph.release();
        std::printf("{\"event\":\"native_ngs2_rack_lock\",\"operation\":\"lock\",\"rack\":%llu,\"thread\":%lu,\"depth\":%u,\"lock_id\":%llu,\"clock_us\":%llu,\"began_clock_us\":%llu,\"wait_us\":%llu}\n", handle, GetCurrentThreadId(), depth, lock_id, acquired, lock_began, acquired - began);
        TraceCommitted("sceNgs2RackLock", handle, caller);
        return 0;
    });
}
int UnlockRack(Handle handle, const void* caller) {
    return Api(caller, [&] {
        unsigned depth{};
        std::uint64_t clock{}, lock_id{}, began{}, held{};
        {
            std::lock_guard lock(registry.mutex);
            const auto found = registry.racks.find(handle);
            if (found == registry.racks.end()) { return kRack; }
            auto& rack = *found->second;
            if (!rack.lock_depth || rack.lock_owner != GetCurrentThreadId()) { return kOperation; }
            if (!--rack.lock_depth) { rack.lock_owner = 0; }
            depth = rack.lock_depth;
            clock = kt::diagnostics::MonotonicMicroseconds();
            lock_id = rack.lock_id; began = rack.lock_began_clock_us;
            if (!depth) { held = clock - began; rack.lock_began_clock_us = 0; rack.lock_id = 0; }
            graph_access.unlock();
        }
        std::printf("{\"event\":\"native_ngs2_rack_lock\",\"operation\":\"unlock\",\"rack\":%llu,\"thread\":%lu,\"depth\":%u,\"lock_id\":%llu,\"clock_us\":%llu,\"began_clock_us\":%llu,\"held_us\":%llu}\n", handle, GetCurrentThreadId(), depth, lock_id, clock, began, held);
        TraceCommitted("sceNgs2RackUnlock", handle, caller);
        return 0;
    });
}
int KT_SYSV RackLock(Handle rack) { return LockRack(rack, __builtin_return_address(0)); }
int KT_SYSV RackUnlock(Handle rack) { return UnlockRack(rack, __builtin_return_address(0)); }
void ApplyEvents(VoiceMemory& voice, SamplerState* sampler) {
    for (unsigned index = 0; index < voice.event_count; ++index) {
        switch (voice.events[index]) {
        case VoiceEvent::Play:
            if (!voice.state) { voice.cursor = 0; if (sampler) { sampler->phase = 0; } }
            voice.state = 1; voice.paused = false;
            voice.flags = kInUse | kPlaying;
            if (sampler && sampler->queued.empty() && !sampler->active) { voice.flags |= kWaveEmpty; }
            if (sampler && sampler->active) { sampler->pcm.Resume(); }
            break;
        case VoiceEvent::Stop: case VoiceEvent::Kill:
            voice.state = 0; voice.paused = false; voice.cursor = 0;
            voice.flags = voice.events[index] == VoiceEvent::Kill ? 0 : (sampler ? kStopped : (voice.channels ? kInUse : 0));
            break;
        case VoiceEvent::Pause:
            if (voice.state) { voice.paused = true; voice.flags |= kPaused; if (sampler) { sampler->pcm.Pause(); } } break;
        case VoiceEvent::Resume:
            if (voice.state) { voice.paused = false; voice.flags &= ~kPaused; if (sampler && sampler->active) { sampler->pcm.Resume(); } } break;
        default: break;
        }
    }
    voice.event_count = 0; voice.event = VoiceEvent::None;
}
std::span<const std::byte> BlockSamples(const PcmBlock& block, const VoiceMemory& voice) {
    const auto stride = voice.channels * (voice.format.type != 24 ? 2u : 4u);
    return std::span<const std::byte>(block.data).subspan(std::size_t{block.skip} * stride, std::size_t{block.samples} * stride);
}
bool ReadyPcm(const VoiceRef& ref, const void* caller) {
    auto& sampler = *ref.sampler; auto& voice = *ref.memory;
    while (!sampler.pcm.Playing()) {
        if (sampler.active) { sampler.phase = sampler.pcm.EndPhase(); sampler.active.reset(); sampler.pcm = {}; }
        if (sampler.queued.empty()) {
            if (sampler.complete) { voice.state = 0; voice.flags = kStopped; }
            else { voice.flags |= kWaveEmpty; }
            return false;
        }
        voice.flags &= ~kWaveEmpty;
        sampler.active = sampler.queued.front(); sampler.queued.erase(sampler.queued.begin());
        if (!sampler.active->samples) { sampler.active.reset(); continue; }
        const auto data = BlockSamples(*sampler.active, voice);
        if (!sampler.pcm.Configure(data, voice.format.type != 24 ? kt::audio::PcmType::Signed16LE : kt::audio::PcmType::Float32LE,
                voice.channels, voice.format.rate, 0, 0, sampler.active->repeats) || !sampler.pcm.PlayFrom(sampler.phase)) {
            UnsupportedApi("native_ngs2_pcm_configuration_invariant", voice.format.type, caller);
        }
        sampler.phase = 0;
    }
    std::span<const std::byte> next;
    for (const auto& block : sampler.queued) {
        if (block->samples) { next = BlockSamples(*block, voice).first(voice.channels * (voice.format.type != 24 ? 2u : 4u)); break; }
    }
    if (!sampler.pcm.SetNextFrame(next) || !sampler.pcm.SetPitch(voice.pitch) || !sampler.pcm.SetGain(voice.gains[0], voice.gains[1])) {
        UnsupportedApi("native_ngs2_pcm_parameter_invariant", 0, caller);
    }
    return true;
}
int Render(Handle handle, const RenderBuffer* buffers, unsigned count, const void* caller) {
    return Api(caller, [&] {
        const auto began = kt::diagnostics::MonotonicMicroseconds();
        std::unique_lock graph(graph_access);
        const auto graph_ready = kt::diagnostics::MonotonicMicroseconds();
        std::unique_lock lock(registry.mutex);
        const auto registry_ready = kt::diagnostics::MonotonicMicroseconds();
        const auto found = registry.systems.find(handle);
        if (found == registry.systems.end()) { return kSystem; }
        if (!buffers || !count || count > 8) { return kAddress; }
        VoiceMemory* master{};
        Handle master_handle{};
        for (const auto& [key, rack] : registry.racks) {
            (void)key;
            if (rack->parent != found->second) { continue; }
            for (unsigned index = 0; index < rack->memory->voices; ++index) {
                auto& voice = rack->voices[index];
                if (rack->memory->id == 0x3000) {
                    if (master || index || rack->memory->voices != 1) { lock.unlock(); UnsupportedApi("sceNgs2SystemRender_multi_master", count, caller); }
                    master = &voice; master_handle = rack->first_voice + index;
                }
            }
        }
        if (!master || count != 1 || !master->channels) { return Error(0x52); }
        const auto output = buffers[0];
        if (!output.buffer) { return kAddress; }
        if (output.channels != master->channels) { return Error(0x52); }
        if (output.waveform != 24) { lock.unlock(); UnsupportedApi("sceNgs2SystemRender_waveform", output.waveform, caller); }
        const auto bytes = std::size_t{found->second->memory->option.grain} * output.channels * 4;
        if (output.size < bytes) { return kSize; }
        auto destination = BeginNativeHostWrite(output.buffer, bytes);
        if (!destination) { return kAddress; }
        std::memset(output.buffer, 0, bytes);
        ApplyEvents(*master, nullptr);
        const auto frames = found->second->memory->option.grain;
        const auto rate = found->second->memory->option.rate;
        auto mix = std::span<float>(reinterpret_cast<float*>(found->second->memory + 1), frames * 2);
        unsigned active_sources{}, starved_sources{}, starved_frames{};
        for (const auto& [key, rack] : registry.racks) {
            (void)key;
            if (rack->parent != found->second || rack->samplers.empty()) { continue; }
            for (unsigned index = 0; index < rack->memory->voices; ++index) {
                auto& voice = rack->voices[index]; auto& sampler = rack->samplers[index];
                ApplyEvents(voice, &sampler);
                if (!voice.state || voice.paused) { continue; }
                ++active_sources; std::fill(mix.begin(), mix.end(), 0);
                VoiceRef source{rack, &voice, &sampler};
                unsigned rendered{};
                for (; rendered < frames && ReadyPcm(source, caller); ++rendered) {
                    if (!sampler.pcm.Mix(mix.subspan(rendered * 2, 2), 2, rate)) { lock.unlock(); UnsupportedApi("native_ngs2_pcm_mix_invariant", index, caller); }
                    ++voice.cursor;
                }
                if (rendered < frames && voice.state && !sampler.complete) { ++starved_sources; starved_frames += frames - rendered; }
                if (sampler.active && !sampler.pcm.Playing() && sampler.queued.empty() && sampler.complete) {
                    sampler.phase = sampler.pcm.EndPhase(); sampler.active.reset(); sampler.pcm = {}; voice.state = 0;
                    voice.flags = kStopped;
                }
                if (!voice.destination || !master->state || master->paused) { continue; }
                if (voice.destination != master_handle) { lock.unlock(); UnsupportedApi("sceNgs2SystemRender_route_profile", voice.destination, caller); }
                for (unsigned frame = 0; frame < frames; ++frame) {
                    for (unsigned channel = 0; channel < output.channels; ++channel) {
                        auto* target = static_cast<std::byte*>(output.buffer) + (std::size_t{frame} * output.channels + channel) * 4;
                        float previous; std::memcpy(&previous, target, 4);
                        double sample_value{};
                        for (unsigned input = 0; input < voice.channels; ++input) {
                            const auto at = channel * voice.channels + input;
                            const auto weight = voice.port_matrix >= 0 && voice.matrix_count ? (at < voice.matrix_count ? voice.matrix[at] : 0.0f) : (channel == input ? 1.0f : 0.0f);
                            sample_value += static_cast<double>(mix[frame * 2 + input]) * weight;
                        }
                        const double sum = previous + sample_value * voice.port_gain * master->gains[channel];
                        const double limit = (std::numeric_limits<float>::max)();
                        const auto sample = static_cast<float>(std::clamp(sum, -limit, limit));
                        std::memcpy(target, &sample, 4);
                    }
                }
            }
        }
        if (master->state && !master->paused) { master->cursor += frames; }
        destination = {}; // Include host-write/cache completion in render timing.
        const auto grain = ++found->second->memory->render_count;
        const auto completed = kt::diagnostics::MonotonicMicroseconds();
        auto& system = *found->second;
        const auto interval = system.last_render_clock_us ? completed - system.last_render_clock_us : 0;
        const auto budget = (std::max)(std::uint64_t{20000}, std::uint64_t{frames} * 2000000 / rate);
        const bool anomaly = completed - began >= budget || interval >= budget || starved_sources;
        const bool report = grain <= 4 || active_sources != system.last_active || grain % 256 == 0 ||
            (anomaly && completed - system.last_timing_report_us >= 100000);
        system.last_render_clock_us = completed;
        if (starved_sources) { ++system.starved_grains; }
        const auto starved_grains = system.starved_grains;
        if (report) { system.last_timing_report_us = completed; }
        found->second->last_active = active_sources;
        lock.unlock(); graph.unlock();
        if (report) {
            std::printf("{\"event\":\"native_ngs2_render_completed\",\"system\":%llu,\"grain\":%llu,\"frames\":%u,\"channels\":%u,\"waveform\":%u,\"bytes\":%zu,\"active_sources\":%u,\"starved_sources\":%u,\"starved_frames\":%u,\"starved_grains\":%llu,\"clock_us\":%llu,\"render_us\":%llu,\"graph_wait_us\":%llu,\"registry_wait_us\":%llu,\"completion_interval_us\":%llu}\n",
                handle, grain, frames, output.channels, output.waveform, bytes, active_sources, starved_sources, starved_frames,
                starved_grains, completed, completed - began, graph_ready - began, registry_ready - graph_ready, interval);
        }
        return 0;
    });
}
int KT_SYSV SystemRender(Handle system, const RenderBuffer* buffers, unsigned count) { return Render(system, buffers, count, __builtin_return_address(0)); }
int KT_SYSV DestroySystem(Handle system, Context* output) { return Destroy(system, output, true, __builtin_return_address(0)); }
int KT_SYSV DestroyRack(Handle rack, Context* output) { return Destroy(rack, output, false, __builtin_return_address(0)); }

__declspec(noinline) int InspectWaveform(const void* data, std::size_t size, const void* caller) {
    // Temporary format frontier: inspect only, never publish a successful parse
    // or touch the guest output. Keep commercial bytes in local run reports.
    std::array<unsigned char, 256> prefix{};
    const auto requested = (std::min)(size, prefix.size());
    SIZE_T captured{};
    if (data && requested) { ReadProcessMemory(GetCurrentProcess(), data, prefix.data(), requested, &captured); }
    captured = (std::min)(captured, static_cast<SIZE_T>(prefix.size()));
    std::array<char, 513> hex{};
    constexpr char digits[] = "0123456789abcdef";
    for (SIZE_T index = 0; index < captured; ++index) { hex[index * 2] = digits[prefix[index] >> 4]; hex[index * 2 + 1] = digits[prefix[index] & 15]; }
    std::printf("{\"event\":\"native_ngs2_waveform_frontier\",\"input_bytes\":%zu,\"captured_bytes\":%zu,\"prefix_hex\":\"%s\",\"guest_output_written\":false}\n", size, static_cast<std::size_t>(captured), hex.data());
    UnsupportedApi("sceNgs2ParseWaveformData_analysis_pending", size, caller);
}
bool WaveInfoWritable(void* output) {
    return output && NativeMemoryWritable(output, sizeof(WaveInfo));
}
__declspec(noinline) int ParseWaveform(const void* data, std::size_t size, WaveInfo* output, const void* caller) {
    return Api(caller, [&] {
        if (!WaveInfoWritable(output)) { return kOutAddress; }
        if (!data) { return Error(0x406); }
        if (size < 4 || size > 16 * 1024 * 1024) { return Error(0x407); }
        const auto base = reinterpret_cast<std::uintptr_t>(data), target = reinterpret_cast<std::uintptr_t>(output);
        if (base > UINTPTR_MAX - size) { return Error(0x406); }
        if (base < target + sizeof(WaveInfo) && target < base + size) { return kOutAddress; }
        std::vector<std::byte> snapshot(size); SIZE_T read{};
        if (!ReadProcessMemory(GetCurrentProcess(), data, snapshot.data(), size, &read) || read != size) { return Error(0x406); }
        WaveInfo info{};
        const bool atrac9=!std::memcmp(snapshot.data(),"RIFF",4);
        const char* codec=atrac9?"atrac9":"hevag";
        if (atrac9) {
            kt::audio::Atrac9Wave wave;
            try { wave=kt::audio::ParseAtrac9Wave(snapshot); }
            catch (const kt::audio::Atrac9Failure& failure) {
                std::printf("{\"event\":\"native_ngs2_waveform_parse_failure\",\"reason\":\"%s\",\"unsupported\":%s}\n",failure.reason,failure.unsupported?"true":"false");
                if (failure.unsupported) { return InspectWaveform(data,size,caller); }
                return Error(0x408);
            }
            info.format={kParsedAtrac9,wave.codec.channels,wave.codec.rate,wave.config,0,0};
            info.offset=wave.data_offset; info.bytes=wave.data_bytes; info.samples=wave.samples; info.delay=wave.delay;
            info.unit_bytes=info.frame_bytes=wave.codec.superframe_bytes;
            info.unit_samples=info.frame_samples=wave.codec.SuperframeSamples(); info.units_per_frame=1;
            const auto append=[&](unsigned begin,unsigned end,unsigned repeats) {
                if (begin<end) { info.block[info.blocks++]={wave.data_offset,wave.data_bytes,repeats,begin,end-begin,0,0}; }
            };
            if (wave.looping) {
                info.loop_begin=wave.loop_start-wave.delay; info.loop_end=wave.loop_end-wave.delay;
                append(wave.delay,wave.loop_start,0);
                append(wave.loop_start,wave.loop_end,wave.loop_count?wave.loop_count-1:UINT32_MAX);
                if (wave.loop_count) { append(wave.loop_end,wave.delay+wave.samples,0); }
            } else { append(wave.delay,wave.delay+wave.samples,0); }
        } else {
        if (std::memcmp(snapshot.data(), "VAGp", 4)) { return InspectWaveform(data, size, caller); }
        kt::audio::VagInfo decoded;
        try { decoded = kt::audio::ParseVag(snapshot); }
        catch (const kt::audio::VagFailure& failure) {
            std::printf("{\"event\":\"native_ngs2_waveform_parse_failure\",\"reason\":\"%s\",\"unsupported\":%s}\n", failure.reason, failure.unsupported ? "true" : "false");
            if (failure.unsupported) { return InspectWaveform(data, size, caller); }
            return Error(0x408);
        }
        info.format = {kParsedHevag, decoded.channels, decoded.rate, 0, 0, 0};
        info.offset = decoded.data_offset; info.bytes = decoded.data_bytes; info.samples = decoded.samples;
        info.unit_bytes = info.frame_bytes = 16 * decoded.channels;
        info.unit_samples = info.frame_samples = 28; info.units_per_frame = 1; info.blocks = 1;
        info.block[0] = {decoded.data_offset, decoded.data_bytes, 0, 0, decoded.samples, 0, 0};
        }
        if (!CopyToNativeMemory(output, &info, sizeof(info))) { return kOutAddress; }
        static std::atomic<unsigned> logged{};
        if (logged.fetch_add(1, std::memory_order_relaxed) < 64) {
            std::printf("{\"event\":\"native_ngs2_waveform_parsed\",\"codec\":\"%s\",\"format_tag\":%u,\"channels\":%u,\"rate\":%u,\"samples\":%u,\"data_offset\":%u,\"data_bytes\":%u,\"blocks\":%u,\"delay\":%u,\"loop_begin\":%u,\"loop_end\":%u}\n",
                codec,info.format.type, info.format.channels, info.format.rate, info.samples, info.offset, info.bytes, info.blocks,info.delay,info.loop_begin,info.loop_end);
        }
        return 0;
    });
}
int KT_SYSV ParseWaveformData(const void* data, std::size_t size, WaveInfo* output) { return ParseWaveform(data, size, output, __builtin_return_address(0)); }

struct FixtureState { unsigned allocs = 0, frees = 0, mode = 0; bool reentered = false, unload_busy = false; Handle parent = 0; };
int KT_SYSV FixtureAllocate(Context* context) {
    auto& state = *reinterpret_cast<FixtureState*>(context->user);
    ++state.allocs; state.reentered = Ngs2Module(Ngs2ModuleAction::IsLoaded) == 0;
    state.unload_busy = Ngs2Module(Ngs2ModuleAction::Unload) == kBusy;
    if (state.parent && Destroy(state.parent, nullptr, true, nullptr) != kOperation) { return kFailure; }
    if (state.mode == 1) { return kEmpty; }
    context->buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, context->size);
    if (!context->buffer) { return kEmpty; }
    if (state.mode == 2) { context->size = 1; }
    return state.mode == 3 ? 1 : 0;
}
int KT_SYSV FixtureFree(Context* context) {
    auto& state = *reinterpret_cast<FixtureState*>(context->user);
    ++state.frees;
    state.reentered &= Ngs2Module(Ngs2ModuleAction::IsLoaded) == 0;
    state.unload_busy &= Ngs2Module(Ngs2ModuleAction::Unload) == kBusy;
    return HeapFree(GetProcessHeap(), 0, context->buffer) ? 0 : kFailure;
}
}

int Ngs2Module(Ngs2ModuleAction action) {
    return Api(nullptr, [&] {
        std::lock_guard lock(registry.mutex);
        if (action == Ngs2ModuleAction::IsLoaded) { return registry.references ? 0 : kNotLoaded; }
        if (action == Ngs2ModuleAction::Load) {
            if (registry.references == UINT32_MAX) { return kEmpty; }
            ++registry.references;
        } else {
            if (!registry.references) { return kNotLoaded; }
            if (registry.references == 1 && (!registry.systems.empty() || !registry.racks.empty() || registry.pending)) { return kBusy; }
            --registry.references;
        }
        std::printf("{\"event\":\"native_system_module\",\"operation\":\"%s\",\"id\":11,\"name\":\"libSceNgs2\",\"references\":%u}\n",
                    action == Ngs2ModuleAction::Load ? "load" : "unload", registry.references);
        return 0;
    });
}
std::uint64_t ResolveNgs2Import(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceNgs2" || module != "libSceNgs2") { return 0; }
    const auto id = identifier.substr(0, identifier.find('#'));
    if (id == "mPYgU4oYpuY") { return reinterpret_cast<std::uint64_t>(&CreateSystem); }
    if (id == "U546k6orxQo") { return reinterpret_cast<std::uint64_t>(&CreateRack); }
    if (id == "MwmHz8pAdAo") { return reinterpret_cast<std::uint64_t>(&RackGetVoice); }
    if (id == "lCqD7oycmIM") { return reinterpret_cast<std::uint64_t>(&DestroyRack); }
    if (id == "u-WrYDaJA3k") { return reinterpret_cast<std::uint64_t>(&DestroySystem); }
    if (id == "uu94irFOGpA") { return reinterpret_cast<std::uint64_t>(&VoiceControl); }
    if (id == "i0VnXM-C9fc") { return reinterpret_cast<std::uint64_t>(&SystemRender); }
    if (id == "MzTa7VLjogY") { return reinterpret_cast<std::uint64_t>(&RackLock); }
    if (id == "++YZ7P9e87U") { return reinterpret_cast<std::uint64_t>(&RackUnlock); }
    if (id == "rEh728kXk3w") { return reinterpret_cast<std::uint64_t>(&VoiceGetStateFlags); }
    if (id == "hyVLT2VlOYk") { return reinterpret_cast<std::uint64_t>(&ParseWaveformData); }
    return 0;
}
int Ngs2WaveformCheck(const wchar_t* path) {
    try {
        std::ifstream stream(std::filesystem::path(path),std::ios::binary|std::ios::ate);
        if (!stream) { throw std::runtime_error("Cannot open waveform"); }
        const auto size=stream.tellg();
        if (size<4 || size>16*1024*1024) { throw std::runtime_error("Waveform size exceeds bounded profile"); }
        std::vector<std::byte> file(static_cast<std::size_t>(size));
        stream.seekg(0); stream.read(reinterpret_cast<char*>(file.data()),size);
        if (!stream) { throw std::runtime_error("Cannot read waveform"); }
        WaveInfo info{};
        const auto parsed=ParseWaveform(file.data(),file.size(),&info,nullptr);
        if (parsed) { std::printf("{\"event\":\"native_waveform_check_failed\",\"parse_status\":%d}\n",parsed); return 2; }
        const auto encoded=std::span<const std::byte>(file).subspan(info.offset,info.bytes);
        const auto began=std::chrono::steady_clock::now();
        const auto pcm=info.format.type==kParsedAtrac9?kt::audio::DecodeAtrac9(encoded,info.format.config):kt::audio::DecodeHevag(encoded,info.format.channels);
        const auto frames=pcm.size()/(info.format.channels*2);
        unsigned peak{}; std::size_t nonzero{};
        for (std::size_t at=0;at<pcm.size();at+=2) {
            std::int16_t sample{}; std::memcpy(&sample,pcm.data()+at,2);
            peak=(std::max)(peak,static_cast<unsigned>(std::abs(static_cast<int>(sample)))); nonzero+=sample!=0;
        }
        for (unsigned n=0;n<info.blocks;++n) {
            if (std::uint64_t{info.block[n].skip}+info.block[n].samples>frames) { throw std::runtime_error("Wave block exceeds decoded source"); }
        }
        const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();
        std::printf("{\"event\":\"native_waveform_check_complete\",\"channels\":%u,\"rate\":%u,\"audible_frames\":%u,\"decoded_frames\":%zu,\"pcm_bytes\":%zu,\"nonzero_samples\":%zu,\"peak\":%u,\"blocks\":%u,\"decode_us\":%lld}\n",
            info.format.channels,info.format.rate,info.samples,frames,pcm.size(),nonzero,peak,info.blocks,micros);
        return 0;
    } catch (const kt::audio::Atrac9Failure& failure) { std::fprintf(stderr,"ATRAC9 check: %s\n",failure.reason); }
      catch (const kt::audio::VagFailure& failure) { std::fprintf(stderr,"VAG check: %s\n",failure.reason); }
      catch (const std::exception& failure) { std::fprintf(stderr,"Waveform check: %s\n",failure.what()); }
    return 2;
}
bool Ngs2SelfTest() {
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"ngs2_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false"); passed &= condition;
    };
    check(kt::audio::HevagSelfTest(), "bounded_hevag_container_and_decoder");
    check(kt::audio::Atrac9SelfTest(), "bounded_atrac9_container_and_decoder");
    FixtureState fixture;
    const Allocator allocator{FixtureAllocate, FixtureFree, reinterpret_cast<std::uintptr_t>(&fixture)};
    Handle system = 73, rack = 0, voice = 0;
    check(Ngs2Module(Ngs2ModuleAction::IsLoaded) == kNotLoaded &&
          SystemCreate(nullptr, &allocator, &system, nullptr, true) == kFailure && system == 73, "not_loaded_has_no_success_handle");
    check(Ngs2Module(Ngs2ModuleAction::Load) == 0 && Ngs2Module(Ngs2ModuleAction::Load) == 0 &&
          Ngs2Module(Ngs2ModuleAction::Unload) == 0, "module_references");
    SystemOption option{sizeof(SystemOption), {}, 0, 512, 256, 48000, {}};
    --option.size;
    check(SystemCreate(&option, &allocator, &system, nullptr, true) == kOptionSize && fixture.allocs == 0 &&
          SystemCreate(nullptr, &allocator, nullptr, nullptr, true) == kOutAddress &&
          SystemCreate(nullptr, nullptr, &system, nullptr, true) == kAllocator && system == 73, "invalid_input_preserves_output");
    ++option.size; option.grain = 1024;
    check(SystemCreate(&option, &allocator, &system, nullptr, true) == kGrain && fixture.allocs == 0, "grain_cannot_exceed_workspace");
    fixture.mode = 1;
    check(SystemCreate(nullptr, &allocator, &system, nullptr, true) == kEmpty && fixture.frees == 0 && system == 73, "allocation_error_publishes_nothing");
    fixture.mode = 2;
    check(SystemCreate(nullptr, &allocator, &system, nullptr, true) == kSize && fixture.frees == 1 && system == 73, "short_allocation_is_returned");
    fixture.mode = 3;
    check(SystemCreate(nullptr, &allocator, &system, nullptr, true) == kFailure && fixture.frees == 2 && system == 73,
          "positive_allocator_status_cannot_masquerade_as_created_handle");
    fixture.mode = 0;
    check(SystemCreate(nullptr, &allocator, &system, nullptr, true, true) == kEmpty && fixture.frees == 3 && system == 73,
          "commit_exception_rolls_back_outside_registry_lock");
    check(SystemCreate(nullptr, &allocator, &system, nullptr, true) == 0 && system != 73 &&
          fixture.reentered && fixture.unload_busy, "live_system_uses_game_allocator_and_userdata");
    check(Ngs2Module(Ngs2ModuleAction::Unload) == kBusy, "live_system_prevents_unload");
    fixture.parent = system;
    check(RackCreate(system, 0x3000, nullptr, &allocator, &rack, nullptr, true) == 0 && fixture.reentered && fixture.unload_busy,
          "rack_allocation_pins_parent_during_reentrant_callback");
    Handle repeated = 0;
    check(GetVoice(rack, 0, &voice) == 0 && GetVoice(rack, 0, &repeated) == 0 && repeated == voice && voice != rack &&
          GetVoice(rack, 1, &repeated) == kVoiceIndex && repeated == voice && GetVoice(rack, 0, nullptr) == kOutAddress,
          "stable_voice_handle_and_index_bounds");
    std::array<unsigned, 2> flag_output{73, 91};
    check(GetFlags(0, flag_output.data(), nullptr) == Error(0x300) && flag_output[0] == 73 &&
          GetFlags(voice, nullptr, nullptr) == kOutAddress && GetFlags(voice, flag_output.data(), nullptr) == 0 &&
          flag_output[0] == 0 && flag_output[1] == 91, "voice_flags_validate_handle_and_write_exactly_four_bytes");
    MasterSetup setup{{sizeof(MasterSetup), 0, 0x30000000}, 2, 0};
    check(Control(voice, &setup.header, nullptr) == 0 && registry.racks.at(rack)->voices[0].channels == 2,
          "master_setup_changes_live_voice_channels");
    setup.channels = 8;
    check(Control(voice, &setup.header, nullptr) == 0 && registry.racks.at(rack)->voices[0].channels == 8,
          "eight_channel_master_configuration_is_not_render_completion");
    check(LockRack(0, nullptr) == kRack && UnlockRack(0, nullptr) == kRack, "rack_lock_rejects_invalid_handles");
    check(UnlockRack(rack, nullptr) == kOperation, "rack_unlock_requires_matching_lock");
    check(LockRack(rack, nullptr) == 0 && LockRack(rack, nullptr) == 0 &&
          Control(voice, &setup.header, nullptr) == 0 && Destroy(rack, nullptr, false, nullptr) == kOperation,
          "recursive_rack_lock_allows_owner_control_and_prevents_destruction");
    std::atomic<bool> attempted{}, completed{};
    int foreign_unlock = 0, worker_control = kFailure, worker_voice = kFailure, worker_destroy = kFailure;
    Handle observed_voice{};
    std::thread worker([&] {
        foreign_unlock = UnlockRack(rack, nullptr);
        attempted.store(true, std::memory_order_release);
        worker_voice = GetVoice(rack, 0, &observed_voice);
        worker_control = Control(voice, &setup.header, nullptr);
        worker_destroy = Destroy(rack, nullptr, false, nullptr);
        completed.store(true, std::memory_order_release);
    });
    const auto wait_for = [](const std::atomic<bool>& flag) {
        const auto until = GetTickCount64() + 2000;
        while (!flag.load(std::memory_order_acquire) && GetTickCount64() < until) { Sleep(1); }
        return flag.load(std::memory_order_acquire);
    };
    const bool began = wait_for(attempted);
    const bool controlled = wait_for(completed);
    if (!controlled) {
        UnlockRack(rack, nullptr); UnlockRack(rack, nullptr); worker.join();
        UnsupportedApi("ngs2_self_test_control_joined_external_rack_lock", rack, nullptr);
    }
    worker.join();
    check(began && foreign_unlock == kOperation && worker_control == 0 && worker_voice == 0 && observed_voice == voice && worker_destroy == kOperation,
          "external_rack_lock_allows_control_and_queries_but_rejects_foreign_unlock_and_destruction");
    std::array<float, 2048> locked_render{}; locked_render.fill(73);
    RenderBuffer locked_buffer{locked_render.data(), sizeof(locked_render), 24, 8};
    std::atomic<bool> render_attempted{}, render_completed{};
    int locked_render_result = kFailure;
    std::thread render_worker([&] {
        render_attempted.store(true, std::memory_order_release);
        locked_render_result = Render(system, &locked_buffer, 1, nullptr);
        render_completed.store(true, std::memory_order_release);
    });
    const bool render_began = wait_for(render_attempted); Sleep(25);
    check(render_began && !render_completed.load(std::memory_order_acquire),
          "external_rack_lock_keeps_render_pending_until_unlock");
    const auto partial_unlock = UnlockRack(rack, nullptr); Sleep(25);
    check(partial_unlock == 0 && !render_completed.load(std::memory_order_acquire), "partial_recursive_unlock_keeps_render_locked");
    const auto final_unlock = UnlockRack(rack, nullptr);
    const bool finished = wait_for(render_completed);
    if (!finished) { UnsupportedApi("ngs2_self_test_worker_stalled", rack, nullptr); }
    render_worker.join();
    check(final_unlock == 0 && locked_render_result == 0 &&
          std::all_of(locked_render.begin(), locked_render.end(), [](auto value) { return value == 0; }),
          "final_rack_unlock_wakes_render_and_completes_real_silent_grain");
    const auto rendered_before_play = registry.systems.at(system)->memory->render_count;
    // Reproduce the game's lock order without hanging the test: the caller
    // holds an explicit rack lock then needs its own voice mutex, while an
    // audio thread owns that mutex and asks for the published voice flags.
    std::timed_mutex voice_mutex;
    std::atomic<bool> query_entered{};
    int query_result = kFailure;
    unsigned query_flags = UINT32_MAX;
    unsigned expected_query_flags = UINT32_MAX;
    const auto initial_query = GetFlags(voice, &expected_query_flags, nullptr);
    const auto query_lock = LockRack(rack, nullptr);
    std::thread query_thread([&] {
        std::lock_guard voice_lock(voice_mutex);
        query_entered.store(true, std::memory_order_release);
        query_result = GetFlags(voice, &query_flags, nullptr);
    });
    const bool query_began = wait_for(query_entered);
    const bool voice_acquired = voice_mutex.try_lock_for(std::chrono::milliseconds(200));
    if (voice_acquired) { voice_mutex.unlock(); }
    const auto query_unlock = UnlockRack(rack, nullptr);
    query_thread.join();
    std::printf("{\"event\":\"ngs2_lock_cycle_diagnostics\",\"query_began\":%s,\"voice_mutex_acquired\":%s,\"query_result\":%d,\"flags\":%u,\"expected_flags\":%u}\n",
        query_began ? "true" : "false", voice_acquired ? "true" : "false", query_result, query_flags, expected_query_flags);
    check(initial_query == 0 && query_lock == 0 && query_unlock == 0 && query_began && voice_acquired && query_result == 0 && query_flags == expected_query_flags,
          "state_query_does_not_participate_in_external_rack_lock_cycle");
    // Actual game order: rack owner -> stream mutex, audio producer ->
    // stream mutex -> VoiceControl. Release the external lock before joining
    // even on regression, so this test diagnoses the cycle instead of hanging.
    std::atomic<bool> control_entered{};
    int control_result = kFailure;
    const auto stream_lock = LockRack(rack, nullptr);
    std::thread stream_thread([&] {
        std::lock_guard stream(voice_mutex);
        control_entered.store(true, std::memory_order_release);
        control_result = Control(voice, &setup.header, nullptr);
    });
    const bool producer_began = wait_for(control_entered);
    const bool stream_acquired = voice_mutex.try_lock_for(std::chrono::milliseconds(200));
    if (stream_acquired) { voice_mutex.unlock(); }
    const auto stream_unlock = UnlockRack(rack, nullptr);
    stream_thread.join();
    check(stream_lock == 0 && stream_unlock == 0 && producer_began && stream_acquired && control_result == 0,
          "voice_control_under_game_stream_mutex_does_not_join_external_rack_lock_cycle");
    check(UnlockRack(rack, nullptr) == kOperation && registry.racks.at(rack)->lock_depth == 0,
          "balanced_rack_lock_leaves_no_owner_or_extra_unlock");
    --setup.header.size;
    check(Control(voice, &setup.header, nullptr) == Error(0x30a) && registry.racks.at(rack)->voices[0].channels == 8 &&
          Control(0, &setup.header, nullptr) == Error(0x300) && Control(voice, nullptr, nullptr) == Error(0x309),
          "invalid_controls_preserve_voice_state");
    EventParam play{{sizeof(EventParam), 0, 6}, 0};
    check(Control(voice, &play.header, nullptr) == 0 && registry.racks.at(rack)->voices[0].event == VoiceEvent::Play &&
          registry.racks.at(rack)->voices[0].state == 0, "ps4_play_event_is_queued_without_faking_a_render_grain");
    check(GetFlags(voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 1,
          "queued_play_does_not_report_a_completed_render_transition");
    play.id = 99;
    check(Control(voice, &play.header, nullptr) == Error(0x303) && registry.racks.at(rack)->voices[0].event == VoiceEvent::Play,
          "invalid_event_does_not_replace_pending_play");
    SamplerOption sampler{{sizeof(SamplerOption), {}, 0, 512, 256, 0, 1, 8, {}}, {256, 256, 4, 4, 8, 256, 256, 32, 8}};
    Handle sampler_rack = 0, last_voice = 0;
    --sampler.rack.size;
    check(RackCreate(system, 0x1000, &sampler.rack, &allocator, &sampler_rack, nullptr, true) == kOptionSize && sampler_rack == 0,
          "sampler_extension_requires_full_168_byte_option");
    ++sampler.rack.size;
    check(RackCreate(system, 0x1000, &sampler.rack, &allocator, &sampler_rack, nullptr, true) == 0 &&
          registry.racks.at(sampler_rack)->memory->limits.ajm_at9_decoders == 32, "sampler_limits_are_saved_in_allocated_context");
    check(GetVoice(sampler_rack, 255, &last_voice) == 0 && last_voice != voice && GetVoice(sampler_rack, 256, &repeated) == kVoiceIndex,
          "sampler_has_256_distinct_bounded_voice_states");
    std::array<unsigned char, 8200> rendered; rendered.fill(73);
    RenderBuffer output{rendered.data(), 8192, 24, 8};
    check(Render(system, &output, 1, nullptr) == 0 &&
          std::all_of(rendered.begin(), rendered.begin() + 8192, [](auto v) { return v == 0; }) && rendered[8192] == 73 &&
          registry.systems.at(system)->memory->render_count == rendered_before_play + 1 && registry.racks.at(rack)->voices[0].state == 1 &&
          registry.racks.at(rack)->voices[0].event == VoiceEvent::None && registry.racks.at(rack)->voices[0].cursor == 256,
          "unconnected_graph_renders_exact_grain_and_consumes_master_play");
    check(GetFlags(voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 3,
          "rendered_master_reports_in_use_and_playing_flags");
    output.size = 8191; rendered.fill(73);
    check(Render(system, &output, 1, nullptr) == kSize && rendered[0] == 73 && registry.systems.at(system)->memory->render_count == rendered_before_play + 1,
          "short_render_buffer_preserves_output_and_graph_clock");
    output.size = 8192; output.channels = 2;
    check(Render(system, &output, 1, nullptr) == Error(0x52) && Render(0, &output, 1, nullptr) == kSystem &&
          Render(system, nullptr, 1, nullptr) == kAddress, "invalid_render_channels_handle_and_descriptor_are_not_success");
    output.channels = 8;
    SamplerSetup pcm_setup{{sizeof(SamplerSetup), 0, 0x10000000}, {18, 1, 48000, 0, 0, 0}, 1, 0};
    auto& sampled = registry.racks.at(sampler_rack)->voices[255];
    check(Control(last_voice, &pcm_setup.header, nullptr) == 0 && sampled.channels == 1 && sampled.format.rate == 48000 && sampled.setup_flags == 1,
          "sampler_setup_commits_pcm_format_without_claiming_playback");
    check(GetFlags(last_voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 0x21,
          "configured_sampler_reports_in_use_and_empty_without_playing");
    pcm_setup.format.channels = 0;
    check(Control(last_voice, &pcm_setup.header, nullptr) == Error(0x52) && sampled.channels == 1,
          "invalid_sampler_setup_preserves_format");
    pcm_setup.format.channels = 1;
    PatchParam patch{{sizeof(PatchParam), 0, 5}, 0, 0, sampler_rack};
    check(Control(last_voice, &patch.header, nullptr) == Error(0x305) && !sampled.destination,
          "sampler_patch_requires_a_live_destination_voice");
    patch.destination = voice;
    check(Control(last_voice, &patch.header, nullptr) == 0 && sampled.destination == voice && Destroy(rack, nullptr, false, nullptr) == kOperation,
          "sampler_route_pins_destination_until_disconnected_or_destroyed");
    const std::array<float, 8> mono_matrix{1, 1, 0, 0, 0, 0, 0, 0};
    MatrixParam matrix{{sizeof(MatrixParam), 0, 1}, 0, 8, mono_matrix.data()};
    check(Control(last_voice, &matrix.header, nullptr) == 0 && sampled.matrix_count == 8 && sampled.matrix[0] == 1 && sampled.matrix[1] == 1,
          "channel_matrix_is_copied_in_destination_major_order");
    matrix.count = 65;
    check(Control(last_voice, &matrix.header, nullptr) == Error(0x151) && sampled.matrix_count == 8,
          "invalid_matrix_dimensions_preserve_committed_levels");
    matrix.count = 8;
    std::array<std::int16_t, 4> samples{-32768, 16384, 32767, 0};
    WaveBlock block{0, sizeof(samples), 0, 0, 4, 0xa5a5a5a5u, 73};
    BlocksParam blocks{{sizeof(BlocksParam), 0, 0x10000001}, samples.data(), 0, 1, &block};
    std::array invalid_batch{block, block}; invalid_batch[1].samples = 5;
    blocks.blocks = invalid_batch.data(); blocks.count = 2;
    check(Control(last_voice, &blocks.header, nullptr) == Error(0x106) && resident_pcm_bytes.load() == 0 &&
          registry.racks.at(sampler_rack)->samplers[255].queued.empty(), "pcm_batch_failure_rolls_back_all_staged_blocks_and_budget");
    blocks.blocks = &block; blocks.count = 1;
    check(Control(last_voice, &blocks.header, nullptr) == 0 && resident_pcm_bytes.load() == sizeof(samples),
          "pcm_blocks_ignore_unused_word_and_capture_owned_bounded_source_bytes");
    play.id = 0; (void)Control(last_voice, &play.header, nullptr);
    samples[0] = 0; rendered.fill(73);
    const auto sample_at = [&](unsigned frame, unsigned channel) {
        float value; std::memcpy(&value, rendered.data() + (frame * 8 + channel) * 4, sizeof(value)); return value;
    };
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == -1 && sample_at(0, 1) == -1 && sample_at(0, 2) == 0 &&
          sample_at(1, 0) == 0.5f && sample_at(2, 0) == 32767.0f / 32768 && sample_at(4, 0) == 0 && rendered[8192] == 73 &&
          sampled.state == 0 && sampled.cursor == 4 && resident_pcm_bytes.load() == 0,
          "pcm16_routes_to_front_pair_and_finishes_short_block_without_reading_reused_input");
    check(GetFlags(last_voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 8,
          "natural_pcm_completion_drops_in_use_and_reports_stopped");
    std::array<std::byte, 80> authored_vag{};
    const auto vag_put = [&](unsigned at, unsigned value) {
        for (unsigned byte = 0; byte < 4; ++byte) { authored_vag[at + byte] = static_cast<std::byte>(value >> (24 - byte * 8)); }
    };
    vag_put(0, 0x56414770); vag_put(4, 0x20001); vag_put(12, 32); vag_put(16, 48000); authored_vag[30] = std::byte{1};
    authored_vag[64] = std::byte{12}; std::fill(authored_vag.begin() + 66, authored_vag.end(), std::byte{0x11});
    struct { WaveInfo info{}; std::uint64_t tail = 73; } parsed;
    check(ParseWaveform(authored_vag.data(), authored_vag.size(), &parsed.info, nullptr) == 0 && parsed.tail == 73 &&
          parsed.info.format.type == kParsedHevag && parsed.info.format.channels == 1 && parsed.info.blocks == 1 &&
          parsed.info.block[0].offset == 48 && parsed.info.block[0].samples == 56, "hevag_parse_publishes_exact_info_and_real_block_extent");
    {
        kt::memory::DirectFixture direct;
        std::memset(direct.first, 73, sizeof(WaveInfo) + 1);
        const auto before = direct.Track();
        check(ParseWaveform(authored_vag.data(), authored_vag.size(), static_cast<WaveInfo*>(direct.alias), nullptr) == 0 &&
            !before.Unchanged() && !std::memcmp(direct.first, &parsed.info, sizeof(WaveInfo)) &&
            static_cast<unsigned char*>(direct.first)[sizeof(WaveInfo)] == 73 && direct.Track().Unchanged(),
            "waveform_parse_host_copy_invalidates_guarded_shared_output_and_preserves_tail");
        check(direct.ProtectAlias(1) && ParseWaveform(authored_vag.data(), authored_vag.size(), static_cast<WaveInfo*>(direct.alias), nullptr) == kOutAddress &&
            !std::memcmp(direct.first, &parsed.info, sizeof(WaveInfo)) && direct.ProtectAlias(3),
            "waveform_parse_preserves_real_readonly_output_instead_of_granting_write_access");
    }
    SamplerSetup hevag_setup{{sizeof(SamplerSetup), 0, 0x10000000}, parsed.info.format, 1, 0};
    BlocksParam hevag_blocks{{sizeof(BlocksParam), 0, 0x10000001}, authored_vag.data(), 0, parsed.info.blocks, parsed.info.block.data()};
    const auto hevag_setup_result = Control(last_voice, &hevag_setup.header, nullptr);
    const auto hevag_queue_result = Control(last_voice, &hevag_blocks.header, nullptr);
    (void)Control(last_voice, &play.header, nullptr);
    check(hevag_setup_result == 0 && hevag_queue_result == 0 && Render(system, &output, 1, nullptr) == 0 &&
          sample_at(0, 0) == 0 && sample_at(28, 0) == 1.0f / 32768 && sample_at(28, 1) == 1.0f / 32768 &&
          sample_at(56, 0) == 0 && sampled.cursor == 56 && resident_pcm_bytes.load() == 0,
          "hevag_parse_sampler_decode_route_render_and_release");
    std::array<std::byte,64> authored_at9{}; authored_at9[4]=std::byte{4};
    SamplerSetup at9_setup{{sizeof(SamplerSetup),0,0x10000000},{kParsedAtrac9,1,48000,0xe00770fe,0,0},1,0};
    WaveBlock at9_block{0,64,0,16,200,0,0};
    BlocksParam at9_blocks{{sizeof(BlocksParam),0,0x10000001},authored_at9.data(),0,1,&at9_block};
    const auto at9_setup_result=Control(last_voice,&at9_setup.header,nullptr);
    const auto at9_queue_result=Control(last_voice,&at9_blocks.header,nullptr);
    check(at9_setup_result==0 && at9_queue_result==0 && resident_pcm_bytes.load()==512,
        "atrac9_queue_accounts_decoded_pcm_not_compressed_bytes");
    authored_at9[0]=std::byte{0x40};
    const auto resident_before_bad_at9=resident_pcm_bytes.load();
    check(Control(last_voice,&at9_blocks.header,nullptr)==Error(0x408) && resident_pcm_bytes.load()==resident_before_bad_at9 &&
        registry.racks.at(sampler_rack)->samplers[255].queued.size()==1,"atrac9_bad_batch_preserves_old_queue_and_budget");
    (void)Control(last_voice,&play.header,nullptr); rendered.fill(73);
    check(Render(system,&output,1,nullptr)==0 && sample_at(0,0)==0 && sample_at(199,0)==0 && sample_at(200,0)==0 &&
        sampled.cursor==200 && sampled.state==0 && resident_pcm_bytes.load()==0 && rendered[8192]==73,
        "atrac9_delay_trim_route_render_and_release_owned_snapshot");
    samples[0] = -32768;
    Handle second_voice{}; (void)GetVoice(sampler_rack, 0, &second_voice);
    (void)Control(last_voice, &pcm_setup.header, nullptr); (void)Control(second_voice, &pcm_setup.header, nullptr);
    (void)Control(second_voice, &patch.header, nullptr);
    (void)Control(second_voice, &matrix.header, nullptr);
    block.offset = 2; block.bytes = 2; block.samples = 1;
    (void)Control(last_voice, &blocks.header, nullptr); (void)Control(second_voice, &blocks.header, nullptr);
    (void)Control(last_voice, &play.header, nullptr); (void)Control(second_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == 1 && sample_at(0, 1) == 1 && sample_at(1, 0) == 0,
          "independent_pcm_voices_accumulate_in_one_master_grain");
    (void)Control(last_voice, &pcm_setup.header, nullptr); block.repeats = UINT32_MAX;
    (void)Control(last_voice, &blocks.header, nullptr); (void)Control(last_voice, &play.header, nullptr); (void)Render(system, &output, 1, nullptr);
    play.id = 1; (void)Control(last_voice, &play.header, nullptr);
    block.repeats = 0; block.offset = 0;
    (void)Control(last_voice, &blocks.header, nullptr); play.id = 0; (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == -1 && sample_at(1, 0) == 0 && !sampled.state,
          "stop_then_new_blocks_then_play_preserves_control_call_order");
    pcm_setup.format.type = 24;
    (void)Control(last_voice, &pcm_setup.header, nullptr);
    const std::array<float, 4> ramp{0, 1, 2, 3};
    blocks.data = ramp.data(); block = {0, sizeof(ramp), UINT32_MAX, 0, 4, 0, 0};
    PitchParam pitch{{sizeof(PitchParam), 0, 0x10000005}, 0.5f, 0};
    (void)Control(last_voice, &pitch.header, nullptr); (void)Control(last_voice, &blocks.header, nullptr);
    play.id = 0; (void)Control(last_voice, &play.header, nullptr); play.id = 4; (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sampled.paused && !sampled.cursor && sample_at(1, 0) == 0,
          "play_then_pause_does_not_consume_queued_pcm");
    check(GetFlags(last_voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 7,
          "paused_voice_retains_playing_and_in_use_flags");
    play.id = 5; (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && !sampled.paused && sampled.cursor == 256 && sample_at(0, 0) == 0 &&
          sample_at(1, 0) == 0.5f && sample_at(2, 0) == 1, "resume_uses_fractional_pitch_and_preserves_stream_position");
    play.id = 3; (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && !sampled.state && resident_pcm_bytes.load() == 0 && sample_at(1, 0) == 0,
          "kill_releases_active_loop_and_pcm_budget");
    check(GetFlags(last_voice, flag_output.data(), nullptr) == 0 && flag_output[0] == 0,
          "kill_clears_all_reported_voice_state_flags");
    pcm_setup.format.type = 18; (void)Control(last_voice, &pcm_setup.header, nullptr);
    play.id = 0; (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sampled.state && sampled.cursor == 0 && sample_at(0, 0) == 0,
          "empty_incomplete_stream_waits_without_inventing_source_samples");
    blocks.data = samples.data(); blocks.flags = 1; block = {0, 4, 0, 0, 2, 0, 0};
    (void)Control(last_voice, &blocks.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sampled.state && sampled.cursor == 2 && sample_at(0, 0) == -1 && sample_at(1, 0) == 0.5f,
          "new_block_resumes_starved_stream_without_a_second_play_event");
    blocks.flags = 0; blocks.count = 0;
    (void)Control(last_voice, &blocks.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && !sampled.state && sample_at(0, 0) == 0,
          "empty_complete_marker_finishes_stream");
    pcm_setup.format.type = 24; pcm_setup.format.rate = 72000; (void)Control(last_voice, &pcm_setup.header, nullptr);
    const std::array<float, 5> stream{0, 1, 2, 3, 4};
    std::array split_blocks{WaveBlock{0, 8, 0, 0, 2, 0, 0}, WaveBlock{8, 12, 0, 0, 3, 0, 0}};
    blocks.data = stream.data(); blocks.blocks = split_blocks.data(); blocks.count = 2;
    (void)Control(last_voice, &blocks.header, nullptr); (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(1, 0) == 1.5f && sample_at(2, 0) == 3 && sample_at(3, 0) == 4 &&
          !sampled.state && resident_pcm_bytes.load() == 0, "ngs2_streaming_resampling_carries_phase_and_interpolates_across_blocks");
    pcm_setup.format.channels = 2; pcm_setup.format.rate = 48000; (void)Control(last_voice, &pcm_setup.header, nullptr);
    const std::array<float, 4> stereo{0.25f, -0.5f, 1, -1};
    std::array<float, 16> stereo_matrix{}; stereo_matrix[0] = 0.5f; stereo_matrix[2] = 0.75f; stereo_matrix[3] = 0.25f; stereo_matrix[15] = 2;
    matrix.count = 16; matrix.levels = stereo_matrix.data(); (void)Control(last_voice, &matrix.header, nullptr);
    blocks.data = stereo.data(); blocks.blocks = &block; blocks.count = 1; block = {0, sizeof(stereo), 0, 0, 2, 0, 0};
    (void)Control(last_voice, &blocks.header, nullptr); (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == 0.125f && sample_at(0, 1) == 0.0625f && sample_at(0, 7) == -1 && sample_at(0, 2) == 0,
          "stereo_matrix_applies_crossfeed_and_all_eight_destination_channels");
    PortMatrixParam port_matrix{{sizeof(PortMatrixParam), 0, 3}, 0, -1};
    (void)Control(last_voice, &port_matrix.header, nullptr); (void)Control(last_voice, &blocks.header, nullptr); (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == 0.25f && sample_at(0, 1) == -0.5f && sample_at(0, 7) == 0,
          "disabled_matrix_uses_identity_channel_routing");
    ValueParam volume{{sizeof(ValueParam), 0, 2}, 0, 0};
    (void)Control(last_voice, &volume.header, nullptr); (void)Control(last_voice, &blocks.header, nullptr); (void)Control(last_voice, &play.header, nullptr);
    check(Render(system, &output, 1, nullptr) == 0 && sample_at(0, 0) == 0 && sampled.cursor == 2 && !sampled.state && resident_pcm_bytes.load() == 0,
          "zero_linear_port_volume_mutes_without_stopping_source_progress");
    {
        kt::memory::DirectFixture direct;
        std::memset(direct.first, 73, 8193);
        const auto before = direct.Track(8192);
        RenderBuffer guarded{direct.alias, 8192, 24, 8};
        check(Render(system, &guarded, 1, nullptr) == 0 && !before.Unchanged() &&
            std::all_of(static_cast<unsigned char*>(direct.first), static_cast<unsigned char*>(direct.first) + 8192, [](auto value) { return value == 0; }) &&
            static_cast<unsigned char*>(direct.first)[8192] == 73 && direct.Track(8192).Unchanged(),
            "audio_render_accepts_guarded_shared_destination_and_invalidates_its_texture_proof");
        const auto grain_before = registry.systems.at(system)->memory->render_count;
        check(direct.ProtectAlias(1) && Render(system, &guarded, 1, nullptr) == kAddress &&
            registry.systems.at(system)->memory->render_count == grain_before && direct.ProtectAlias(3),
            "audio_render_rejects_real_readonly_output_without_advancing_graph");
    }
    check(Destroy(sampler_rack, nullptr, false, nullptr) == 0 && GetVoice(sampler_rack, 255, &repeated) == kRack && fixture.frees == 4,
          "sampler_destruction_invalidates_all_owned_voice_handles");
    check(Destroy(system, nullptr, true, nullptr) == kOperation, "parent_destruction_requires_empty_graph");
    check(Destroy(rack, nullptr, false, nullptr) == 0 && GetVoice(rack, 0, &repeated) == kRack &&
          Destroy(rack, nullptr, false, nullptr) == kRack && fixture.frees == 5, "rack_free_once_and_stale_handle_rejected");
    fixture.parent = 0;
    check(Destroy(system, nullptr, true, nullptr) == 0 && Destroy(system, nullptr, true, nullptr) == kSystem &&
          fixture.frees == 6 && fixture.reentered && fixture.unload_busy, "system_free_once_outside_registry_lock");
    check(Ngs2Module(Ngs2ModuleAction::Unload) == 0 && Ngs2Module(Ngs2ModuleAction::Unload) == kNotLoaded,
          "unload_after_all_contexts_released");
    check(ResolveNgs2Import("mPYgU4oYpuY", "wrong", "libSceNgs2") == 0 &&
          ResolveNgs2Import("mPYgU4oYpuY", "libSceNgs2", "wrong") == 0 &&
          ResolveNgs2Import("MzTa7VLjogY", "wrong", "libSceNgs2") == 0 &&
          ResolveNgs2Import("++YZ7P9e87U", "libSceNgs2", "wrong") == 0 &&
          ResolveNgs2Import("rEh728kXk3w", "wrong", "libSceNgs2") == 0 &&
          ResolveNgs2Import("rEh728kXk3w", "libSceNgs2", "wrong") == 0 &&
          ResolveNgs2Import("unknown", "libSceNgs2", "libSceNgs2") == 0, "only_implemented_exports_are_bound");
    std::printf("{\"event\":\"ngs2_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
