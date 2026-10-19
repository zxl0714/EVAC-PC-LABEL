#include "native_resident_gpu.h"
#include "native_video.h"
#include "graphics/vulkan_device.h"
#include "gpu_core/gpu_worker.h"
#include "gpu_core/native_memory_backend.h"
#include "runtime_api.h"
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
    explicit Impl(const std::filesystem::path& path) {
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Resident GPU DLL must be an existing absolute file path");
        }
        library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) { throw std::runtime_error("Cannot load resident GPU DLL"); }
        try {
            const auto get = reinterpret_cast<GetRuntimeApi>(GetProcAddress(library, "KetsuiGpuRuntimeGetApi"));
            if (!get || !get(RuntimeApiVersion, sizeof(api), &api)) { throw std::runtime_error("Resident GPU SDK version mismatch"); }
            const HostSettings settings{.log_directory = path.parent_path(), .presentation_support = true,
                .vsync = GetNativeDisplaySettings().vsync};
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
NativeResidentGpu::NativeResidentGpu(const std::filesystem::path& path) {
    if (active) { throw std::runtime_error("Resident GPU already exists"); }
    impl = std::make_unique<Impl>(path);
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
    Run([&] { RuntimeError error; Check(impl->api.drain(impl->context, &error), error); });
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
    Run([&] {
        const auto layout = impl->api.layout();
        Registers(layout.num_indices, {&vertices, 1});
        Registers(layout.num_instances, {&instances, 1});
        RuntimeError error;
        Check(impl->api.draw(impl->context, false, 0, &error), error);
    });
}
kt::gfx::PresentStatus NativeResidentGpu::Present(std::uint64_t address, const kt::gfx::DisplayLayout& layout,
    kt::diagnostics::BackendPresentTiming* timing) {
    if (timing) { *timing = {}; }
    RuntimePresentResult result;
    const RuntimeDisplay display{address, layout.width, layout.height, layout.pitch,
        layout.format == kt::gfx::ColorFormat::Bgra8Srgb ? 0x80000000u : 0x80002200u, layout.tiled ? 0u : 1u};
    Run([&] { RuntimeError error; Check(impl->api.present(impl->context, &display, &result, &error), error); });
    if (timing && result.status == RuntimePresentStatus::Completed) { *timing = result.timing; }
    switch (result.status) {
    case RuntimePresentStatus::Completed: return kt::gfx::PresentStatus::Completed;
    case RuntimePresentStatus::ZeroExtent: return kt::gfx::PresentStatus::ZeroExtent;
    case RuntimePresentStatus::OutOfDate: return kt::gfx::PresentStatus::OutOfDate;
    }
    throw std::runtime_error("Invalid resident GPU presentation result");
}
bool NativeResidentGpu::Read(std::uint64_t address, void* output, std::uint64_t bytes) {
    if (!impl->memory.IsMapped(address, bytes)) { return false; }
    Run([&] { impl->memory.ReadMetadata(address, output, bytes); });
    return true;
}
