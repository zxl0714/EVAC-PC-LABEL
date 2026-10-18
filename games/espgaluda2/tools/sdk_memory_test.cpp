// Authored allocation regression, without game data or interactive systems.
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
using rex::X_STATUS;

void Require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Usage: sdk_tests <NEW-isolated-directory>");
        const auto root = std::filesystem::absolute(argv[1]);
        Require(!std::filesystem::exists(root), "Test directory must be new");
        std::filesystem::create_directories(root / "empty-game");
        rex::InitLoggingEarly();
        auto logging = rex::BuildLogConfig(nullptr, "off", {});
        rex::InitLogging(logging);
        rex::Runtime runtime(root / "empty-game", root / "user-data", {}, root / "cache", root / "metadata");
        Require(runtime.Setup({.kernel_init=rex::kernel::InitializeKernel, .tool_mode=true}) == X_STATUS_SUCCESS,
                "Tool runtime setup");
        Require(!runtime.graphics_system() && !runtime.audio_system() && !runtime.input_system(), "Interactive subsystem created");
        auto* heap = runtime.memory()->LookupHeap(0x82000000);
        constexpr auto flags = rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit;
        constexpr auto protect = rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite;
        Require(heap->AllocFixed(0x82000000, 0x20000, 0x10000, flags, protect), "Main allocation");
        Require(heap->AllocFixed(0x88000000, 0x10000, 0x10000, flags, protect), "Overlay allocation");
        Require(!heap->AllocFixed(0x88000000, 0x10000, 0x10000, flags, protect), "Combined reserve+commit allowed overlap");
        Require(!heap->AllocFixed(0x81FF0000, 0x20000, 0x10000, flags, protect), "Partial overlap was accepted");
        Require(heap->Release(0x88000000), "Overlay release");
        rex::memory::HeapAllocationInfo main{};
        Require(heap->QueryRegionInfo(0x82000000, &main) && main.allocation_base == 0x82000000 &&
                main.allocation_size == 0x20000 && main.state == flags, "Main allocation changed");
        Require(heap->AllocFixed(0x88000000, 0x10000, 0x10000, flags, protect), "Overlay reload");
        Require(heap->Release(0x88000000) && heap->Release(0x82000000), "Image release");
        runtime.Shutdown();
        std::cout << "SDK authored memory regression passed; guest execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
