#include "native_resident_gpu.h"
#include "native_video.h"
#include "native_profile.h"
#include "graphics/snapshot_handoff.h"
#include "graphics/vulkan_device.h"
#include "gpu_core/gpu_worker.h"
#include "gpu_core/native_memory_backend.h"
#include "runtime_api.h"
#include "native_async_log.h"
#include <windows.h>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
using namespace Ketsui::GpuCore;
NativeResidentGpu* active;
void Check(bool ok, const RuntimeError& error) {
    if (!ok) { throw std::runtime_error(error.message); }
}
}
struct NativeResidentGpu::Impl {
    HMODULE library{};
    RuntimeApi api{};
    RuntimeRegisterLayout register_layout{};
    GpuWorker worker;
    void* context{};
    std::map<std::uint64_t, NativeMemoryRangeInfo> mappings;
    NativeMemoryCacheBackend memory{Hooks()};
    NativeCacheHooks Hooks() {
        return {
            .read_program = [](auto address, void* output, auto bytes) {
                SIZE_T copied{};
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output,
                    static_cast<SIZE_T>(bytes), &copied) || copied != bytes) {
                    throw std::runtime_error("Shader code is not readable native memory");
                }
            },
            .sync = [this](auto task) { worker.Sync(std::move(task)); },
            .is_gpu_thread = [this] { return worker.IsCurrent(); },
            .mapped = [this](const auto& range) {
                RuntimeError error;
                Check(api.map(context, range.address, range.bytes, &error), error);
                mappings[range.mapping_address] = range;
            },
            .before_change = [this](auto ranges) { BeforeChange(ranges); },
            .access = [this](auto ranges, bool write) {
                for (const auto& range : ranges) {
                    RuntimeError error;
                    Check(api.access(context, range.address, range.bytes, write, &error), error);
                }
            },
            .drain = [this] {
                std::vector<NativeMemoryRangeInfo> all;
                for (const auto& [base, range] : mappings) { (void)base; all.push_back(range); }
                BeforeChange(all);
                RuntimeError error;
                Check(api.drain(context, &error), error);
            }
        };
    }
    void BeforeChange(std::span<const NativeMemoryRangeInfo> ranges) {
        for (const auto& range : ranges) {
            RuntimeError error;
            for (const auto& alias : NativeMemoryAliases(range.address, range.bytes)) {
                Check(api.access(context, alias.address, alias.bytes, true, &error), error);
            }
            Check(api.unmap(context, range.address, range.bytes, &error), error);
            mappings.erase(range.mapping_address);
        }
    }
    explicit Impl(const std::filesystem::path& path, bool validation) {
        if (kt::diagnostics::TimingTraceEnabled()) {
            worker.SetObserver([](const GpuWorker::Timing& t) noexcept {
                NativeLogf("{\"event\":\"native_gpu_worker_handoff\",\"began_clock_us\":%llu,\"enqueued_clock_us\":%llu,\"started_clock_us\":%llu,\"finished_clock_us\":%llu,\"clock_us\":%llu,\"worker_cpu_100ns\":%llu,\"caller_thread\":%u,\"worker_thread\":%u,\"queue_depth\":%u}\n",
                    t.began,t.enqueued,t.started,t.finished,t.resumed,t.cpu_100ns,t.caller,t.worker,t.depth);
            });
        }
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Resident GPU DLL must be an existing absolute file path");
        }
        library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) { throw std::runtime_error("Cannot load resident GPU DLL"); }
        try {
            const auto get = reinterpret_cast<GetRuntimeApi>(GetProcAddress(library, "KetsuiGpuRuntimeGetApi"));
            if (!get || !get(RuntimeApiVersion, sizeof(api), &api)) { throw std::runtime_error("Resident GPU SDK version mismatch"); }
            register_layout = api.layout();
            if (register_layout.num_indices >= register_layout.bank_words || register_layout.num_instances >= register_layout.bank_words ||
                register_layout.index_base >= register_layout.bank_words - 1 || register_layout.index_type >= register_layout.bank_words ||
                register_layout.max_index_size >= register_layout.bank_words || register_layout.draw_initiator >= register_layout.bank_words) {
                throw std::runtime_error("Resident GPU draw register layout is invalid");
            }
            const HostSettings settings{.validation_core = validation, .validation_sync = validation,
                .neo = GetNativeProfile().neo,
                .log_directory = path.parent_path(), .presentation_support = true,
                .vsync = GetNativeDisplaySettings().vsync,
                .diagnostic_sink = [](const char* data, std::size_t bytes) noexcept { return WriteNativeLog({data, bytes}); }};
            worker.Sync([&] {
                RuntimeError error;
                Check(api.create(&settings, &memory, &context, &error), error);
            });
            memory.Attach();
        } catch (...) {
            if (context) {
                worker.Sync([&] { RuntimeError error; Check(api.destroy(context, &error), error); context = nullptr; });
            }
            FreeLibrary(library); library = nullptr;
            throw;
        }
    }
    ~Impl() {
        try {
            memory.Detach();
            worker.Sync([&] { RuntimeError error; Check(api.destroy(context, &error), error); context = nullptr; });
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Resident GPU teardown failed: %s\n", error.what());
            std::terminate();
        }
        FreeLibrary(library);
    }
};
NativeResidentGpu::NativeResidentGpu(const std::filesystem::path& path, bool validation) {
    if (active) { throw std::runtime_error("Resident GPU already exists"); }
    impl = std::make_unique<Impl>(path, validation);
    active = this;
    std::printf("{\"event\":\"native_gpu_backend\",\"backend\":\"resident\",\"sdk\":%u,\"attached_before_guest_entry\":true}\n", RuntimeApiVersion);
}
NativeResidentGpu::~NativeResidentGpu() { active = nullptr; }
NativeResidentGpu* CurrentNativeResidentGpu() { return active; }
void NativeResidentGpu::Run(std::function<void()> operation) { impl->worker.Sync(std::move(operation)); }
void NativeResidentGpu::SetWindow(void* window) {
    Run([&] { RuntimeError error; Check(impl->api.set_window(impl->context, window, &error), error); });
}
void NativeResidentGpu::Drain() {
    const auto began = kt::diagnostics::TimingTraceEnabled() ? kt::diagnostics::MonotonicMicroseconds() : 0;
    Run([&] { RuntimeError error; Check(impl->api.drain(impl->context, &error), error); });
    if (began) NativeLogf("{\"event\":\"native_gpu_drain\",\"began_clock_us\":%llu,\"clock_us\":%llu,\"thread\":%lu}\n",began,kt::diagnostics::MonotonicMicroseconds(),GetCurrentThreadId());
}
void NativeResidentGpu::Flush() {
    Run([&] {
        RuntimeError error;
        std::uint64_t tick{};
        Check(impl->api.flush(impl->context, &tick, &error), error);
    });
}
void NativeResidentGpu::FinishGuest() {
    const auto began = kt::diagnostics::TimingTraceEnabled() ? kt::diagnostics::MonotonicMicroseconds() : 0;
    Run([&] { RuntimeError error; Check(impl->api.finish_guest(impl->context, &error), error); });
    if (began) NativeLogf("{\"event\":\"native_gpu_guest_fence\",\"began_clock_us\":%llu,\"clock_us\":%llu,\"thread\":%lu}\n",began,kt::diagnostics::MonotonicMicroseconds(),GetCurrentThreadId());
}
void NativeResidentGpu::Clear() {
    Run([&] { RuntimeError error; Check(impl->api.clear(impl->context, &error), error); });
}
void NativeResidentGpu::Registers(std::uint32_t offset, std::span<const std::uint32_t> words) {
    Run([&] {
        RuntimeError error;
        Check(impl->api.registers(impl->context, offset, words.data(), static_cast<std::uint32_t>(words.size()), &error), error);
    });
}
void NativeResidentGpu::Extent(unsigned target, unsigned width, unsigned height) {
    Run([&] { RuntimeError error; Check(impl->api.extent(impl->context, target, width, height, &error), error); });
}
void NativeResidentGpu::Draw(unsigned vertices, unsigned instances) {
    const auto began = kt::diagnostics::TimingTraceEnabled() ? kt::diagnostics::MonotonicMicroseconds() : 0;
    Run([&] {
        RuntimeError error;
        // Already on the owning worker. Keep both guarded register writes, but
        // avoid recursively wrapping each one in another Sync/std::function.
        Check(impl->api.registers(impl->context, impl->register_layout.num_indices, &vertices, 1, &error), error);
        Check(impl->api.registers(impl->context, impl->register_layout.num_instances, &instances, 1, &error), error);
        Check(impl->api.draw(impl->context, false, 0, &error), error);
    });
    if (began) {
        const auto ended = kt::diagnostics::MonotonicMicroseconds();
        if (ended-began >= 250) NativeLogf("{\"event\":\"native_gpu_slow_draw\",\"began_clock_us\":%llu,\"clock_us\":%llu,\"vertices\":%u,\"instances\":%u,\"thread\":%lu}\n",began,ended,vertices,instances,GetCurrentThreadId());
    }
}
void NativeResidentGpu::DrawIndexed(std::uint64_t address, unsigned count, unsigned max_count, unsigned instances, unsigned index_type) {
    if (index_type > 1 || !address || (address & ((2u << index_type) - 1)) || count > max_count ||
        address > 0xffffffffffull || std::uint64_t{count} * (2u << index_type) > 0x10000000000ull - address) {
        throw std::runtime_error("Invalid resident index buffer descriptor");
    }
    Run([&] {
        RuntimeError error;
        const auto word = [&](unsigned offset, unsigned value) { Check(impl->api.registers(impl->context, offset, &value, 1, &error), error); };
        // The register layout is high word then low word, unlike DRAW_INDEX_2.
        const std::uint32_t base[]{static_cast<std::uint32_t>(address >> 32), static_cast<std::uint32_t>(address)};
        Check(impl->api.registers(impl->context, impl->register_layout.index_base, base, 2, &error), error);
        word(impl->register_layout.index_type, index_type);
        word(impl->register_layout.max_index_size, max_count);
        word(impl->register_layout.num_indices, count);
        word(impl->register_layout.num_instances, instances);
        word(impl->register_layout.draw_initiator, 0);
        Check(impl->api.draw(impl->context, true, 0, &error), error);
    });
}
kt::gfx::PresentStatus NativeResidentGpu::Present(std::uint64_t address, const kt::gfx::DisplayLayout& layout,
    kt::diagnostics::BackendPresentTiming* timing, const kt::diagnostics::PresentationPacing* pacing) {
    if (timing) { *timing = {}; }
    RuntimePresentResult result;
    const RuntimeDisplay display{address, layout.width, layout.height, layout.pitch,
        layout.format == kt::gfx::ColorFormat::Bgra8Srgb ? 0x80000000u : 0x80002200u, layout.tiled ? 0u : 1u};
    Run([&] { RuntimeError error; Check(impl->api.prepare_present(impl->context, &display, &result, &error), error); });
    if (result.status == RuntimePresentStatus::Prepared) {
        const bool accepted=cave::ps4::PublishThenPace([&] {
            Run([&] { RuntimeError error; Check(impl->api.complete_present(impl->context, true, &result, &error), error); });
        },[&] { return !pacing || pacing->wait(pacing->context); });
        // The owned image is already safe for the output worker. Cancellation
        // still prevents guest label release / flip completion at the caller.
        if(!accepted)result.status=RuntimePresentStatus::Cancelled;
    }
    if (timing && result.status == RuntimePresentStatus::Completed) { *timing = result.timing; }
    switch (result.status) {
    case RuntimePresentStatus::Completed:
        if (!result.guest_copy_completed) throw std::runtime_error("Guest buffer released before snapshot completion");
        return kt::gfx::PresentStatus::Completed;
    case RuntimePresentStatus::ZeroExtent: return kt::gfx::PresentStatus::ZeroExtent;
    case RuntimePresentStatus::OutOfDate: return kt::gfx::PresentStatus::OutOfDate;
    case RuntimePresentStatus::Cancelled: return kt::gfx::PresentStatus::Cancelled;
    case RuntimePresentStatus::Prepared: break; // never a completed host flip
    }
    throw std::runtime_error("Invalid resident GPU presentation result");
}
bool NativeResidentGpu::Read(std::uint64_t address, void* output, std::uint64_t bytes) {
    if (!impl->memory.IsMapped(address, bytes)) { return false; }
    Run([&] { impl->memory.ReadMetadata(address, output, bytes); });
    return true;
}
void NativeResidentGpu::ReadColorForDiagnostics(std::uint64_t address, unsigned width, unsigned height,
    std::span<std::uint32_t> output) {
    if (!width || !height || std::uint64_t{width} * height != output.size()) {
        throw std::runtime_error("Diagnostic color readback extent mismatch");
    }
    Run([&] {
        RuntimeError error;
        Check(impl->api.read_color(impl->context, address, width, height, output.data(), output.size_bytes(), &error), error);
    });
}
