// Authored dispatcher regression: no ROM, guest thread, window or controller.
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <rex/logging.h>
#include <rex/system/export_resolver.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>

namespace {
constexpr uint32_t kMain = 0x82000000, kOverlay = 0x88000000;
constexpr uint32_t kCode = 0x10000, kImage = 0x100000;
void Host(PPCContext&, uint8_t*) {}
void ModeA(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 11; }
void ModeB(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 22; }
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void RegisterMain(rex::runtime::IModuleRegistrar* r) { r->SetFunction(kMain + 0x10, &Host); }
void RegisterA(rex::runtime::IModuleRegistrar* r) { r->SetFunction(kOverlay + 0x10, &ModeA); }
void RegisterB(rex::runtime::IModuleRegistrar* r) { r->SetFunction(kOverlay + 0x10, &ModeB); }
}

int main() {
    try {
        rex::InitLoggingEarly();
        rex::InitLogging(rex::BuildLogConfig(nullptr, "off", {}));
        rex::memory::Memory memory;
        Require(memory.Initialize(), "Memory initialization failed");
        rex::runtime::ExportResolver resolver;
        rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);
        Require(dispatcher.InitializeFunctionTable(kMain, kCode, kMain, kImage, true), "Main table");
        dispatcher.RegisterModule("main", kMain, RegisterMain);
        Require(dispatcher.InitializeFunctionTable(kOverlay, kCode, kOverlay, kImage), "Overlay table");
        dispatcher.RegisterModule("mode-a", kOverlay, RegisterA);
        const auto mainThunk = dispatcher.AllocateThunk(&Host, kMain + 0x10);
        const auto overlayThunk = dispatcher.AllocateThunk(&Host, kOverlay + 0x10);
        Require(mainThunk && overlayThunk && mainThunk != overlayThunk, "Caller pools are not independent");
        Require(dispatcher.AllocateThunk(&Host, 0) == mainThunk, "Host lookup must reuse the main pool");
        // Beyond the previous 16,384-entry pool exhaustion, without a timed game run.
        for (unsigned i = 0; i < 100000; ++i) {
            Require(dispatcher.AllocateThunk(&Host, kMain + 0x20) == mainThunk, "Repeated lookup consumed another thunk");
            Require(dispatcher.AllocateThunk(&Host, kOverlay + 0x20) == overlayThunk, "Overlay lookup consumed another thunk");
        }
        std::atomic<bool> consistent{true};
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < 4; ++t) threads.emplace_back([&] {
            for (unsigned i = 0; i < 10000; ++i)
                if (dispatcher.AllocateThunk(&Host, kMain + 0x20) != mainThunk) consistent = false;
        });
        for (auto& thread : threads) thread.join();
        Require(consistent, "Concurrent lookup returned unstable addresses");
        Require(!dispatcher.AllocateThunk(&Host, 0xDEADBEEF), "Invalid caller was accepted");
        Require(!dispatcher.AllocateThunk(nullptr, kMain + 0x20), "Null function was cached");
        Require(dispatcher.GetFunction(mainThunk) == &Host, "Main thunk target changed");
        const auto exportA = dispatcher.ResolveGuestProcedure(kOverlay + 0x10, kMain + 0x10);
        Require(exportA && dispatcher.GetFunction(exportA) == &ModeA, "Guest export resolution failed");
        auto nativeSlot = [&](uint32_t address) {
            return *memory.TranslateVirtual<PPCFunc**>(kMain + kImage + (address - kMain) * 2);
        };
        Require(nativeSlot(exportA) == &ModeA, "Native dispatch table differs from lookup");
        Require(dispatcher.ResolveGuestProcedure(kOverlay + 0x10, 0) == kOverlay + 0x10,
                "Host direct-export lookup changed");
        Require(!dispatcher.ResolveGuestProcedure(0, kMain + 0x10), "Missing export changed");
        Require(dispatcher.ResolveGuestProcedure(kOverlay + 0x44, kMain + 0x10) == kOverlay + 0x44,
                "Non-recompiled export changed");
        Require(!dispatcher.ResolveGuestProcedure(kOverlay + 0x10, 0xDEADBEEF), "Invalid export caller accepted");
        for (unsigned i = 0; i < 100000; ++i)
            Require(dispatcher.ResolveGuestProcedure(kOverlay + 0x10, kMain + 0x20) == exportA,
                    "Repeated guest export consumed another thunk");
        const auto removed = dispatcher.UnregisterModule("mode-a");
        Require(removed && removed->first == overlayThunk && removed->second == overlayThunk + 4,
                "Repeated lookups grew the overlay pool");
        Require(!dispatcher.GetFunction(overlayThunk), "Unloaded caller thunk survived");
        Require(!dispatcher.GetFunction(exportA), "Resident caller retained an unloaded DLL function");
        Require(!nativeSlot(exportA), "Native dispatch slot retained an unloaded DLL function");
        Require(dispatcher.GetFunction(mainThunk) == &Host, "Overlay unload damaged main thunk");
        Require(dispatcher.InitializeFunctionTable(kOverlay, kCode, kOverlay, kImage), "Reload table");
        dispatcher.RegisterModule("mode-b", kOverlay, RegisterB);
        const auto fresh = dispatcher.AllocateThunk(&ModeB, kOverlay + 0x10);
        Require(fresh == overlayThunk && dispatcher.GetFunction(fresh) == &ModeB, "Reload reused stale thunk cache");
        const auto exportB = dispatcher.ResolveGuestProcedure(kOverlay + 0x10, kMain + 0x10);
        Require(exportB && exportB != exportA && dispatcher.GetFunction(exportB) == &ModeB,
                "Same-address mode switch retained the previous export");
        Require(!dispatcher.GetFunction(exportA), "Old export pointer aliases the new mode");
        PPCContext context{};
        nativeSlot(exportB)(context, memory.virtual_membase());
        Require(context.r3.u64 == 22, "Native thunk called the wrong authored mode");
        dispatcher.SetFunction(kOverlay + 0x10, &ModeA);
        const auto rebound = dispatcher.ResolveGuestProcedure(kOverlay + 0x10, kMain + 0x10);
        Require(rebound && rebound != exportB && dispatcher.GetFunction(rebound) == &ModeA,
                "Rebound export retained old target");
        Require(!dispatcher.GetFunction(exportB), "Rebound export did not invalidate old slot");
        dispatcher.UnregisterModule("mode-b");
        Require(!dispatcher.GetFunction(rebound), "Rebound export survived module unload");
        Require(!nativeSlot(rebound), "Rebound native slot survived module unload");
        dispatcher.UnregisterModule("main");
        Require(!dispatcher.AllocateThunk(&Host, 0), "Missing entrypoint accepted");
        std::cout << "Procedure thunk reuse, concurrency and caller reload passed; game execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
