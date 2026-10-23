#pragma once
#include "graphics/display_layout.h"
#include "diagnostics/present_timing.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
namespace kt::gfx { enum class PresentStatus; }

class NativeResidentGpu {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit NativeResidentGpu(const std::filesystem::path& library, bool validation = true);
    ~NativeResidentGpu();
    NativeResidentGpu(const NativeResidentGpu&) = delete;
    void Run(std::function<void()> operation);
    void SetWindow(void* window);
    void Drain();
    void FinishGuest();
    // Submit recorded draws without waiting; the later present fence still
    // covers them on the same queue. No flip/event/guest-memory completion.
    void Flush();
    void Clear();
    void Registers(std::uint32_t offset, std::span<const std::uint32_t> words);
    void Extent(unsigned target, unsigned width, unsigned height);
    void Draw(unsigned vertices, unsigned instances);
    kt::gfx::PresentStatus Present(std::uint64_t address, const kt::gfx::DisplayLayout& layout,
        kt::diagnostics::BackendPresentTiming* timing = nullptr,
        const kt::diagnostics::PresentationPacing* pacing = nullptr);
    bool Read(std::uint64_t address, void* output, std::uint64_t bytes);
    // Authored offline fixtures only; never part of the normal frame path.
    void ReadColorForDiagnostics(std::uint64_t address, unsigned width, unsigned height,
        std::span<std::uint32_t> output);
};
NativeResidentGpu* CurrentNativeResidentGpu();
