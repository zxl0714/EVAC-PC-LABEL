#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>

namespace Ketsui::GpuCore {
enum class CompileKind { GraphicsPipeline, ComputePipeline, Shader, TilingPipeline };
// Cache misses only: no per-draw logging, game bytes, allocations or GPU waits.
class ScopedCompileMetrics {
public:
    ScopedCompileMetrics(CompileKind kind, std::uint64_t key) noexcept
        : name_(Name(kind)), key_(key), id_(next_id_.fetch_add(1, std::memory_order_relaxed) + 1),
          began_(Clock()), exceptions_(std::uncaught_exceptions()) {
        std::printf("{\"event\":\"native_gpu_compile_begin\",\"id\":%llu,\"kind\":\"%s\",\"key\":\"0x%llx\",\"clock_us\":%llu}\n",
                    U(id_), name_, U(key_), U(began_));
    }
    ~ScopedCompileMetrics() noexcept {
        const auto ended = Clock();
        std::printf("{\"event\":\"native_gpu_compile_completed\",\"id\":%llu,\"kind\":\"%s\",\"key\":\"0x%llx\",\"began_clock_us\":%llu,\"clock_us\":%llu,\"duration_us\":%llu,\"unwinding\":%s}\n",
                    U(id_), name_, U(key_), U(began_), U(ended), U(ended - began_),
                    std::uncaught_exceptions() > exceptions_ ? "true" : "false");
    }
    ScopedCompileMetrics(const ScopedCompileMetrics&) = delete;
    ScopedCompileMetrics& operator=(const ScopedCompileMetrics&) = delete;
private:
    static unsigned long long U(std::uint64_t value) { return static_cast<unsigned long long>(value); }
    static std::uint64_t Clock() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static const char* Name(CompileKind kind) {
        switch (kind) {
        case CompileKind::GraphicsPipeline: return "graphics_pipeline";
        case CompileKind::ComputePipeline: return "compute_pipeline";
        case CompileKind::Shader: return "shader";
        case CompileKind::TilingPipeline: return "tiling_pipeline";
        }
        return "unknown";
    }
    inline static std::atomic<std::uint64_t> next_id_{};
    const char* name_;
    std::uint64_t key_, id_, began_;
    int exceptions_;
};
}
