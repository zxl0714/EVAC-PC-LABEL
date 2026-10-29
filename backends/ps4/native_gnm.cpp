#include "native_gnm.h"
#include "native_async_log.h"
#include "native_diagnostics.h"
#include "native_host_timing.h"
#include "native_profile.h"
#include "native_video.h"
#include "native_memory.h"
#include "native_gpu.h"
#include "native_resident_gpu.h"
#include "memory/direct_fixture.h"
#include "graphics/pm4.h"
#include "graphics/shader_binary.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>

#define KT_SYSV __attribute__((sysv_abi))
namespace {
// Local resource bounds, not console API limits. Keep the startup draft
// journal small independently of the commands accepted by the real backend.
constexpr unsigned kMaxSubmitStreams = 16;
constexpr std::uint32_t kMaxSubmitStreamBytes = 1024 * 1024;
constexpr std::uint64_t kMaxSubmitSnapshotBytes = 4ull * 1024 * 1024;
constexpr unsigned kDraftObservationWords = 16384;
struct Draft { std::uintptr_t start{}; std::uint32_t capacity{}; };
thread_local Draft draft;
struct SubmissionQueue {
    std::mutex mutex;
    kt::gfx::pm4::Analysis state;
    unsigned owner{};
    std::uint64_t completed{}, frames{};
    std::chrono::steady_clock::time_point last_present{};
    std::int64_t last_record_log_us{};
};
SubmissionQueue& Queue() { static SubmissionQueue queue; return queue; }
// ReadProcessMemory copies untrusted metadata without turning a bad pointer
// into an access violation in the host. Command/shader payloads additionally
// require a live CPU-readable direct-memory lease, not just a readable page.
bool ReadMetadata(const void* source, void* output, std::size_t bytes) {
    SIZE_T copied{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, output, bytes, &copied) && copied == bytes;
}
bool SnapshotDirect(std::uintptr_t address, std::span<std::uint32_t> output) {
    if (!address || address % 4 || output.empty()) { return false; }
    std::uint64_t physical{};
    auto* source = reinterpret_cast<void*>(address);
    if (!RetainNativeMemoryRange(source, output.size_bytes(), &physical, 1)) { return false; }
    struct Pin { void* address; ~Pin() { ReleaseNativeMemoryRange(address, 1); } } pin{source};
    if (auto* resident = CurrentNativeResidentGpu()) { return resident->Read(address, output.data(), output.size_bytes()); }
    return ReadMetadata(source, output.data(), output.size_bytes());
}
bool ReadShaderMemory(void*, std::uint64_t address, void* output, std::size_t bytes) {
    if (!bytes || address > UINTPTR_MAX) { return false; }
    auto* source = reinterpret_cast<void*>(address); std::uint64_t physical{};
    if (!RetainNativeMemoryRange(source, bytes, &physical, 1)) { return false; }
    struct Pin { void* address; ~Pin() { ReleaseNativeMemoryRange(address, 1); } } pin{source};
    return ReadMetadata(source, output, bytes);
}
void PrintWords(const char* kind, unsigned index, std::uint64_t address, std::span<const std::uint32_t> words) {
    // One complete line even when other guest threads emit diagnostics.
    _lock_file(stdout);
    std::printf("{\"event\":\"native_gnm_snapshot\",\"kind\":\"%s\",\"index\":%u,\"address\":\"0x%llx\",\"dwords\":%zu,\"gpu_executed\":false,\"words\":[", kind, index, address, words.size());
    for (std::size_t i = 0; i < words.size(); ++i) { std::printf(i ? ",%u" : "%u", words[i]); }
    std::printf("]}\n"); _unlock_file(stdout);
}
void InspectShader(const kt::gfx::pm4::Analysis& journal, unsigned low, const char* stage) {
    if (CurrentNativeResidentGpu()) { return; }
    const auto lo = journal.shader.Get(low), hi = journal.shader.Get(low + 1);
    if (!lo || !hi || !*lo || *hi > 0xff) { return; }
    const auto address = (static_cast<std::uint64_t>(*lo) << 8) | (static_cast<std::uint64_t>(*hi) << 40);
    // Prefix only. This does not infer the instruction length by scanning for
    // an END token, and the prefix length is not a shader binary size.
    std::array<std::uint32_t, 256> prefix{};
    if (SnapshotDirect(address, prefix)) { PrintWords(stage, 0, address, prefix); }
    try {
        const auto binary = kt::gfx::ReadShaderBinary(ReadShaderMemory, nullptr, address);
        const bool vertex = low == 0x48;
        if (binary.stage != (vertex ? kt::gfx::ShaderStage::Vertex : kt::gfx::ShaderStage::Pixel)) { throw std::runtime_error("Shader stage does not match its binding"); }
        const auto compiled = kt::gfx::CompileShader(binary.stage, binary.instructions, journal.shader, journal.context, ReadShaderMemory, nullptr, 0, address);
        PrintWords(vertex ? "vertex_spirv" : "pixel_spirv", 0, address, compiled.spirv);
        PrintWords(vertex ? "vertex_flat_constants" : "pixel_flat_constants", 0, address, compiled.flat_constants);
        for (const auto& binding : compiled.buffers) {
            std::printf("{\"event\":\"native_gnm_shader_buffer\",\"stage\":\"%s\",\"binding\":%u,\"address\":\"0x%llx\",\"bytes\":%llu,\"flat_constants\":%s,\"writable\":%s}\n",
                vertex ? "vertex" : "pixel", binding.binding, binding.address, binding.bytes, binding.flat_constants ? "true" : "false", binding.writable ? "true" : "false");
        }
        std::printf("{\"event\":\"native_gnm_shader_conversion\",\"stage\":\"%s\",\"code_bytes\":%zu,\"metadata_offset\":%u,\"spirv_words\":%zu,\"buffer_bindings\":%u,\"image_bindings\":%u,\"sampler_bindings\":%u,\"converted\":true,\"gpu_executed\":false}\n",
            vertex ? "vertex" : "pixel", binary.instructions.size() * 4, binary.metadata_offset, compiled.spirv.size(), compiled.buffer_bindings, compiled.image_bindings, compiled.sampler_bindings);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[gcn conversion %s] %s\n", stage, error.what());
        std::printf("{\"event\":\"native_gnm_shader_conversion\",\"stage\":\"%s\",\"converted\":false,\"gpu_executed\":false}\n", low == 0x48 ? "vertex" : "pixel");
    }
}
enum class SubmitResult { Completed, CloseRequested };
__declspec(noinline) SubmitResult SubmitAndFlip(std::uint32_t count, const std::uint32_t* const* dcb_addresses,
    const std::uint32_t* dcb_sizes, const std::uint32_t* const* ccb_addresses, const std::uint32_t* ccb_sizes,
    std::uint32_t video, std::uint32_t buffer, std::uint32_t flip_mode, std::int64_t flip_argument, const void* caller,
    const char*& cancelled_stage) {
    try {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        const auto micros = [](auto begin, auto end) { return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count(); };
        std::int64_t diagnostic_log_us{};
        auto log_started = Clock::now();
        static std::atomic<unsigned> observed{};
        const bool observe = observed.fetch_add(1, std::memory_order_relaxed) < 2;
        NativeLogf("{\"event\":\"native_gnm_submit_inspect\",\"count\":%u,\"video\":%u,\"buffer\":%u,\"flip_mode\":%u,\"flip_argument\":\"0x%llx\",\"ccb_addresses_present\":%s,\"ccb_sizes_present\":%s,\"accepted\":false,\"gpu_executed\":false}\n",
            count, video, buffer, flip_mode, static_cast<unsigned long long>(flip_argument), ccb_addresses ? "true" : "false", ccb_sizes ? "true" : "false");
        diagnostic_log_us += micros(log_started, Clock::now());
        if (!count || count > kMaxSubmitStreams) { UnsupportedApi("native_gnm_submit_inspection_count", count, caller); }
        if (ccb_addresses || ccb_sizes) { UnsupportedApi("native_gnm_submit_ccb_inspection_pending", count, caller); }
        std::array<std::uintptr_t, kMaxSubmitStreams> addresses{};
        std::array<std::uint32_t, kMaxSubmitStreams> sizes{};
        if (!ReadMetadata(dcb_addresses, addresses.data(), count * sizeof(addresses[0])) ||
            !ReadMetadata(dcb_sizes, sizes.data(), count * sizeof(sizes[0]))) {
            UnsupportedApi("native_gnm_submit_unreadable_metadata", count, caller);
        }
        // Validate ALL extents and the combined allocation budget before
        // dereferencing any payload or producing a partial submission.
        std::uint64_t snapshot_bytes{};
        for (unsigned index = 0; index < count; ++index) {
            if (!sizes[index] || sizes[index] % 4 || sizes[index] > kMaxSubmitStreamBytes) {
                UnsupportedApi("native_gnm_submit_inspection_extent", sizes[index], caller);
            }
            if (sizes[index] > kMaxSubmitSnapshotBytes - snapshot_bytes) {
                UnsupportedApi("native_gnm_submit_inspection_budget", snapshot_bytes + sizes[index], caller);
            }
            snapshot_bytes += sizes[index];
        }
        std::vector<std::vector<std::uint32_t>> streams;
        streams.reserve(count);
        for (unsigned index = 0; index < count; ++index) {
            std::vector<std::uint32_t> words(sizes[index] / 4);
            if (!SnapshotDirect(addresses[index], words)) { UnsupportedApi("native_gnm_submit_unregistered_dcb", addresses[index], caller); }
            if (observe) { PrintWords("dcb", index, addresses[index], words); }
            const auto journal = kt::gfx::pm4::Analyze(words);
            log_started = Clock::now();
            NativeLogf("{\"event\":\"native_gnm_submit_journal\",\"index\":%u,\"bytes\":%u,\"packets\":%u,\"draws\":%u,\"complete\":%s,\"error\":%u,\"error_offset\":%u,\"error_header\":\"0x%x\",\"gpu_executed\":false}\n",
                index, sizes[index], journal.packets, journal.draws, journal.Complete() ? "true" : "false", static_cast<unsigned>(journal.error), journal.error_offset,
                journal.Complete() ? 0 : words[journal.error_offset]);
            diagnostic_log_us += micros(log_started, Clock::now());
            if (observe) { InspectShader(journal, 0x48, "vertex_prefix"); InspectShader(journal, 8, "pixel_prefix"); }
            if (!journal.Complete()) {
                // Keep the exact bounded, already-owned stream even after the
                // first two startup observations. A header alone cannot tell
                // a new packet variant from malformed payload or truncation.
                PrintWords("invalid_dcb", index, addresses[index], words);
                UnsupportedApi("native_gnm_submit_invalid_stream", journal.error_offset, caller);
            }
            streams.push_back(std::move(words));
        }
        const auto snapshot_ready = Clock::now();
        auto& queue = Queue(); std::lock_guard lock(queue.mutex);
        const auto queue_ready = Clock::now();
        if (queue.owner != video) { queue.state = {}; queue.owner = video; queue.last_present = {}; queue.last_record_log_us = 0; }
        NativeGpuExecution completion;
        const auto executed = WithNativeVideoGpu(static_cast<int>(video), buffer, [&](auto& target) {
            try { completion = ExecuteNativeGpuCommands(target, streams, queue.state, ReadShaderMemory); }
            catch (...) {
                // Capture the actual failing stream and draw's live shader
                // state, not merely the first two (usually clear-only) frames.
                for (unsigned index = 0; index < streams.size(); ++index) { PrintWords("frontier_dcb", index, addresses[index], streams[index]); }
                InspectShader(queue.state, 0x48, "vertex_prefix"); InspectShader(queue.state, 8, "pixel_prefix");
                throw;
            }
            log_started = Clock::now();
            NativeLogf("{\"event\":\"native_gpu_submit_execution\",\"draws_completed\":%u,\"prepared_flip\":%s,\"presented\":false}\n", completion.draws, completion.prepared_flip ? "true" : "false");
            diagnostic_log_us += micros(log_started, Clock::now());
        });
        const auto executed_at = Clock::now();
        if (executed == NativeVideoResult::CloseRequested) { cancelled_stage = "execute"; return SubmitResult::CloseRequested; }
        if (executed == NativeVideoResult::Completed) {
            if (!completion.prepared_flip) { UnsupportedApi("native_gnm_submit_missing_prepare_flip", count, caller); }
            NativeVideoPresentTiming present_timing;
            const auto presented = PresentNativeVideoGpu(static_cast<int>(video), buffer, flip_mode, flip_argument, &present_timing);
            if (presented == NativeVideoResult::CloseRequested) { cancelled_stage = "present"; return SubmitResult::CloseRequested; }
            if (presented != NativeVideoResult::Completed || !present_timing.completed) { UnsupportedApi("native_gnm_submit_present_incomplete", buffer, caller); }
            ++queue.completed;
            const auto completed_at = Clock::now();
            const auto interval = queue.last_present == Clock::time_point{} ? 0 : micros(queue.last_present, completed_at);
            log_started = Clock::now();
            NativeLogf("{\"event\":\"native_gnm_frame_completed\",\"submission\":%llu,\"draws\":%u,\"snapshot_us\":%lld,\"queue_wait_us\":%lld,\"execute_us\":%lld,\"present_us\":%lld,\"total_us\":%lld,\"completion_interval_us\":%lld,\"completed_clock_us\":%lld,\"diagnostic_log_us\":%lld,\"previous_frame_log_us\":%lld,\"host_thread_id\":%lu,\"present_fence_completed\":true,\"present_stages\":{\"duration_us\":%llu,\"lock_wait_us\":%llu,\"prepare_us\":%llu,\"display_clock_wait_us\":%llu,\"backend_present_us\":%llu,\"completion_us\":%llu},\"backend_stages\":{\"completed\":%s,\"duration_us\":%llu,\"prepare_us\":%llu,\"retire_us\":%llu,\"acquire_us\":%llu,\"record_us\":%llu,\"submit_us\":%llu,\"queue_present_us\":%llu,\"gpu_wait_us\":%llu,\"present_wait_us\":%llu,\"finalize_us\":%llu,\"between_phases_us\":%llu},\"command_stages\":{\"duration_us\":%llu,\"draw_us\":%llu,\"acquire_us\":%llu,\"prefetch_us\":%llu,\"flush_us\":%llu,\"other_us\":%llu,\"acquires\":%u,\"prefetches\":%u}}\n",
                queue.completed, completion.draws, micros(started, snapshot_ready), micros(snapshot_ready, queue_ready), micros(queue_ready, executed_at),
                micros(executed_at, completed_at), micros(started, completed_at), interval, micros(Clock::time_point{}, completed_at),
                diagnostic_log_us, queue.last_record_log_us, GetCurrentThreadId(), present_timing.duration_us,
                present_timing.lock_wait_us, present_timing.prepare_us, present_timing.display_clock_wait_us,
                present_timing.backend_present_us, present_timing.completion_us,
                present_timing.backend.completed ? "true" : "false", present_timing.backend.duration_us,
                present_timing.backend.prepare_us, present_timing.backend.retire_us, present_timing.backend.acquire_us,
                present_timing.backend.record_us, present_timing.backend.submit_us, present_timing.backend.queue_present_us,
                present_timing.backend.gpu_wait_us, present_timing.backend.present_wait_us, present_timing.backend.finalize_us, present_timing.backend.between_phases_us,
                completion.duration_us, completion.draw_us, completion.acquire_us, completion.prefetch_us, completion.flush_us,
                completion.other_us, completion.acquires, completion.prefetches);
            RecordNativeHostFrameTiming(queue.completed, micros(Clock::time_point{}, completed_at), interval);
            // Formatting/enqueue time is outside the completed frame's scope.
            // It belongs to the next interval; disk writing is worker-only.
            queue.last_record_log_us = micros(log_started, Clock::now());
            queue.last_present = completed_at;
            return SubmitResult::Completed;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[GPU execution] %s\n", error.what());
        UnsupportedApi("native_gnm_submit_execution_frontier", count, caller);
    } catch (...) { UnsupportedApi("native_gnm_submit_inspection_host_failure", count, caller); }
    // No queue submission, buffer-label mutation, flip event, or success
    // return is allowed until an execution backend actually completes it.
    UnsupportedApi("sceGnmSubmitAndFlipCommandBuffers_execution_pending", count, caller);
}
int KT_SYSV InspectSubmitAndFlip(std::uint32_t count, const std::uint32_t* const* dcb_addresses,
    const std::uint32_t* dcb_sizes, const std::uint32_t* const* ccb_addresses, const std::uint32_t* ccb_sizes,
    std::uint32_t video, std::uint32_t buffer, std::uint32_t flip_mode, std::int64_t flip_argument) {
    const auto* caller = __builtin_return_address(0);
    const char* cancelled_stage{};
    const auto result = SubmitAndFlip(count, dcb_addresses, dcb_sizes, ccb_addresses, ccb_sizes, video, buffer, flip_mode, flip_argument, caller, cancelled_stage);
    // The helper has released the queue/video locks, snapshots, and pins. Stop
    // only on a typed cancellation, never on a coincidentally closed window
    // after an unrelated validation or execution failure.
    if (result == SubmitResult::CloseRequested) { StopForNativeWindowClose(cancelled_stage, video, buffer, caller); }
    return 0;
}
int KT_SYSV SubmitDone() {
    auto& queue = Queue(); std::lock_guard lock(queue.mutex);
    // The queue is synchronous: every accepted submission already completed
    // its GPU/writeback and guest-snapshot fences before returning. WSI consumes
    // that independent snapshot asynchronously. Taking the same queue lock is
    // the guest frame barrier, not a fabricated GPU or scanout completion.
    ++queue.frames;
    if (queue.frames <= 4) {
        std::printf("{\"event\":\"native_gnm_submit_done\",\"frames\":%llu,\"completed_submissions\":%llu,\"queue_idle\":true}\n", queue.frames, queue.completed);
    }
    return 0;
}
// One read-only journal of the first locally initialized command draft. This
// observes construction, not submission, and never advances a GPU or a fence.
__declspec(noinline) void ObserveDraft(std::uint32_t* last_command, std::uint32_t size) {
    const auto last = reinterpret_cast<std::uintptr_t>(last_command);
    if (!draft.start || last < draft.start || (last - draft.start) % 4) { return; }
    const auto previous = (last - draft.start) / 4;
    if (previous > kDraftObservationWords || size > kDraftObservationWords - previous || previous + size > draft.capacity) { return; }
    static std::atomic_flag captured = ATOMIC_FLAG_INIT;
    if (captured.test()) { return; }
    auto* start = reinterpret_cast<std::uint32_t*>(draft.start);
    const auto count = static_cast<std::uint32_t>(previous + size);
    std::uint64_t physical{};
    if (!RetainNativeMemoryRange(start, static_cast<std::uint64_t>(count) * 4, &physical, 1)) { return; }
    struct Pin { void* address; ~Pin() { ReleaseNativeMemoryRange(address, 1); } } pin{start};
    if (captured.test_and_set()) { return; }
    const auto journal = kt::gfx::pm4::Analyze({start, count});
    const auto header = journal.Complete() ? 0 : start[journal.error_offset];
    std::array<std::uint32_t, 6> body{}; unsigned body_words = 0;
    if (!journal.Complete()) {
        const auto decoded = kt::gfx::pm4::Decode({start, count}, journal.error_offset);
        if (decoded.error == kt::gfx::pm4::Error::None) {
            body_words = static_cast<unsigned>(decoded.packet.body.size());
            for (std::size_t i = 0; i < body.size() && i < decoded.packet.body.size(); ++i) { body[i] = decoded.packet.body[i]; }
        }
    }
    std::printf("{\"event\":\"native_gnm_draft_journal\",\"dwords\":%u,\"packets\":%u,\"register_writes\":%u,\"draws\":%u,\"waits\":%u,\"dma_transfers\":%u,\"complete\":%s,\"error\":%u,\"error_offset\":%u,\"error_header\":\"0x%x\",\"error_body_words\":%u,\"error_body_prefix\":[%u,%u,%u,%u,%u,%u],\"first_dma_source\":\"0x%llx\",\"first_dma_bytes\":%u,\"last_dma_source\":\"0x%llx\",\"last_dma_bytes\":%u,\"gpu_executed\":false}\n",
        count, journal.packets, journal.register_writes, journal.draws, journal.waits, journal.dma_transfers, journal.Complete() ? "true" : "false", static_cast<unsigned>(journal.error), journal.error_offset, header,
        body_words, body[0], body[1], body[2], body[3], body[4], body[5], journal.first_dma ? journal.first_dma->source : 0ull,
        journal.first_dma ? journal.first_dma->bytes : 0, journal.last_dma ? journal.last_dma->source : 0ull, journal.last_dma ? journal.last_dma->bytes : 0);
}
std::uint32_t KT_SYSV Hardware350(std::uint32_t* output, std::uint32_t capacity) {
    const auto caller = __builtin_return_address(0);
    if (capacity < kt::gfx::pm4::kHardware350Words || !output || reinterpret_cast<std::uintptr_t>(output) % alignof(std::uint32_t)) { return 0; }
    std::array<std::uint32_t, kt::gfx::pm4::kHardware350Words> commands{};
    const auto size = kt::gfx::pm4::WriteHardware350(commands,GetNativeProfile().neo);
    const auto analysis = kt::gfx::pm4::Analyze(commands);
    if (!size || !analysis.Complete()) { UnsupportedApi("native_gnm_init_sequence_invalid", analysis.error_offset, caller); }
    std::memcpy(output, commands.data(), sizeof(commands));
    draft = {reinterpret_cast<std::uintptr_t>(output), capacity};
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) {
        std::printf("{\"event\":\"native_gnm_command_init\",\"version\":350,\"dwords\":%u,\"packets\":%u,\"register_writes\":%u,\"gpu_executed\":false}\n", size, analysis.packets, analysis.register_writes);
        TraceNativeApi("sceGnmDrawInitDefaultHardwareState350", size, caller);
    }
    return size;
}
int KT_SYSV WaitFlip(std::uint32_t* output, std::uint32_t size, int handle, unsigned index) {
    if (!output || size != 7 || reinterpret_cast<std::uintptr_t>(output) % alignof(std::uint32_t)) { return -1; }
    const auto label = AcquireNativeVideoBufferLabel(handle, index);
    if (!label.owner) { return -1; }
    if (!kt::gfx::pm4::WriteWaitFlip({output, size}, label.address)) { return -1; }
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set()) {
        std::printf("{\"event\":\"native_gnm_wait_flip_command\",\"handle\":%d,\"buffer\":%u,\"dwords\":7,\"gpu_executed\":false}\n", handle, index);
        TraceNativeApi("sceGnmInsertWaitFlipDone", label.address, __builtin_return_address(0));
    }
    return 0;
}
__declspec(noinline) int VertexShaderImpl(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers, std::uint32_t modifier, bool update, const void* caller) {
    if (!output || !registers || size < 29 || reinterpret_cast<std::uintptr_t>(output) % 4 || reinterpret_cast<std::uintptr_t>(registers) % 4) { return -1; }
    // Retain values for logging before any possible output/input alias writes.
    const auto address = static_cast<std::uint64_t>(registers[0]) << 8;
    if (!kt::gfx::pm4::WriteVertexShader({output, size}, {registers, 7}, modifier, update)) { return -1; }
    static std::atomic<unsigned> traced[2]{};
    if (traced[update].fetch_add(1, std::memory_order_relaxed) < 4) {
        std::printf("{\"event\":\"native_gnm_shader_command\",\"stage\":\"vertex\",\"dwords\":29,\"shader_address\":\"0x%llx\",\"gpu_executed\":false}\n", address);
        TraceNativeApi(update ? "sceGnmUpdateVsShader" : "sceGnmSetVsShader", address, caller);
    }
    return 0;
}
int KT_SYSV VertexShader(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers, std::uint32_t modifier) {
    return VertexShaderImpl(output, size, registers, modifier, false, __builtin_return_address(0));
}
int KT_SYSV UpdateVertexShader(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers, std::uint32_t modifier) {
    return VertexShaderImpl(output, size, registers, modifier, true, __builtin_return_address(0));
}
__declspec(noinline) int PixelShader350Impl(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers, bool update, const void* caller) {
    if (!output || size < 40 || reinterpret_cast<std::uintptr_t>(output) % 4 || reinterpret_cast<std::uintptr_t>(registers) % 4) { return -1; }
    const auto address = registers ? static_cast<std::uint64_t>(registers[0]) << 8 : 0;
    if (!kt::gfx::pm4::WritePixelShader350({output, size}, registers ? std::span<const std::uint32_t>(registers, 12) : std::span<const std::uint32_t>{}, update)) { return -1; }
    static std::atomic<unsigned> traced[2]{};
    if (traced[update].fetch_add(1, std::memory_order_relaxed) < 4) {
        std::printf("{\"event\":\"native_gnm_shader_command\",\"stage\":\"pixel\",\"dwords\":40,\"shader_address\":\"0x%llx\",\"gpu_executed\":false}\n", address);
        TraceNativeApi(update ? "sceGnmUpdatePsShader350" : "sceGnmSetPsShader350", address, caller);
    }
    return 0;
}
int KT_SYSV PixelShader350(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers) {
    return PixelShader350Impl(output, size, registers, false, __builtin_return_address(0));
}
int KT_SYSV UpdatePixelShader350(std::uint32_t* output, std::uint32_t size, const std::uint32_t* registers) {
    return PixelShader350Impl(output, size, registers, true, __builtin_return_address(0));
}
int KT_SYSV DrawAuto(std::uint32_t* output, std::uint32_t size, std::uint32_t vertices, std::uint32_t flags) {
    if (!output || size != 7 || reinterpret_cast<std::uintptr_t>(output) % 4) { return -1; }
    if (!kt::gfx::pm4::WriteDrawAuto({output, size}, vertices, flags,GetNativeProfile().neo)) { return -1; }
    ObserveDraft(output, size);
    static std::atomic<unsigned> traced{};
    if (traced.fetch_add(1, std::memory_order_relaxed) < 4) {
        std::printf("{\"event\":\"native_gnm_draw_command\",\"vertices\":%u,\"dwords\":7,\"gpu_executed\":false}\n", vertices);
        TraceNativeApi("sceGnmDrawIndexAuto", vertices, __builtin_return_address(0));
    }
    return 0;
}
int KT_SYSV DrawIndex(std::uint32_t* output, std::uint32_t size, std::uint32_t count,
    std::uint64_t address, std::uint32_t flags, std::uint32_t type) {
    if (!output || size != 10 || reinterpret_cast<std::uintptr_t>(output) % 4) { return -1; }
    if (!kt::gfx::pm4::WriteDrawIndex({output, size}, count, address, flags,GetNativeProfile().neo)) { return -1; }
    ObserveDraft(output, size);
    static std::atomic<unsigned> traced{};
    if (traced.fetch_add(1, std::memory_order_relaxed) < 4) {
        std::printf("{\"event\":\"native_gnm_indexed_draw_command\",\"indices\":%u,\"address\":\"0x%llx\",\"type\":%u,\"dwords\":10,\"gpu_executed\":false}\n", count, address, type);
        TraceNativeApi("sceGnmDrawIndex", count, __builtin_return_address(0));
    }
    return 0;
}
}
std::uint64_t ResolveGnmImport(std::string_view identifier, std::string_view library, std::string_view module) {
    if (library != "libSceGnmDriver" || module != "libSceGnmDriver") { return 0; }
    if (identifier.substr(0, identifier.find('#')) == "yb2cRhagD1I") { return reinterpret_cast<std::uint64_t>(&Hardware350); }
    if (identifier.substr(0, identifier.find('#')) == "1qXLHIpROPE") { return reinterpret_cast<std::uint64_t>(&WaitFlip); }
    if (identifier.substr(0, identifier.find('#')) == "gAhCn6UiU4Y") { return reinterpret_cast<std::uint64_t>(&VertexShader); }
    if (identifier.substr(0, identifier.find('#')) == "5uFKckiJYRM") { return reinterpret_cast<std::uint64_t>(&PixelShader350); }
    if (identifier.substr(0, identifier.find('#')) == "mLVL7N7BVBg") { return reinterpret_cast<std::uint64_t>(&UpdatePixelShader350); }
    if (identifier.substr(0, identifier.find('#')) == "V31V01UiScY") { return reinterpret_cast<std::uint64_t>(&UpdateVertexShader); }
    if (identifier.substr(0, identifier.find('#')) == "GGsn7jMTxw4") { return reinterpret_cast<std::uint64_t>(&DrawAuto); }
    if (identifier.substr(0, identifier.find('#')) == "HlTPoZ-oY7Y") { return reinterpret_cast<std::uint64_t>(&DrawIndex); }
    if (identifier.substr(0, identifier.find('#')) == "xbxNatawohc") { return reinterpret_cast<std::uint64_t>(&InspectSubmitAndFlip); }
    if (identifier.substr(0, identifier.find('#')) == "yvZ73uQUqrk") { return reinterpret_cast<std::uint64_t>(&SubmitDone); }
    return 0;
}

bool GnmCloseSelfTest(int handle) {
    kt::memory::DirectFixture memory;
    auto* commands = static_cast<std::uint32_t*>(memory.first);
    commands[0] = 0xc0001000u; commands[1] = 0; // one authored NOP packet
    const std::uint32_t* address = commands;
    const std::uint32_t bytes = 8;
    const auto completed_before = Queue().completed;
    const char* stage{};
    const auto result = SubmitAndFlip(1, &address, &bytes, nullptr, nullptr,
        static_cast<unsigned>(handle), 0, 1, 0, nullptr, stage);
    // A different host thread must be able to acquire the submission lock
    // after cancellation returns; never rely on recursive same-thread locking.
    bool queue_released{};
    std::thread observer([&] {
        std::unique_lock lock(Queue().mutex, std::try_to_lock);
        queue_released = lock.owns_lock();
    });
    observer.join();
    const bool cancelled = result == SubmitResult::CloseRequested && stage && std::strcmp(stage, "execute") == 0;
    const bool no_completion = Queue().completed == completed_before;
    memory.Reset(); // fails if the command snapshot retained a direct-memory pin
    const bool passed = cancelled && queue_released && no_completion;
    std::printf("{\"event\":\"gnm_close_self_test\",\"cancelled\":%s,\"queue_lock_released\":%s,\"completed_frame_unchanged\":%s,\"command_memory_released\":true,\"passed\":%s}\n",
        cancelled ? "true" : "false", queue_released ? "true" : "false", no_completion ? "true" : "false", passed ? "true" : "false");
    return passed;
}
