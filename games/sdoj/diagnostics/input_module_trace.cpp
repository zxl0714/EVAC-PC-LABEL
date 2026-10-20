#include "input_trace_policy.h"
#include <rex/ppc.h>
#include <windows.h>

namespace {
using Sink = void (*)(std::uint32_t, std::uint32_t, std::uint32_t);
struct Bridge {
    unsigned module;
    Sink sink;
    Sink render;
    explicit Bridge(unsigned id) : module(id), sink(reinterpret_cast<Sink>(
        GetProcAddress(GetModuleHandleW(nullptr), "SdojCoreInputEvent"))), render(reinterpret_cast<Sink>(
        GetProcAddress(GetModuleHandleW(nullptr), "SdojRenderEvent"))) {
        // This object is local to the loaded game DLL. Re-loading that DLL
        // resets the host observer rather than pairing against an old scene.
        if (sink) sink(module, 0, 0);
        if (render) render(module, 0, 0);
    }
    void Built() const { if (sink) sink(module, 1, sdoj::diagnostics::CoreInputBlock(module)); }
    void Copy(std::uint32_t block) const {
        if (sink) sink(module, 2, block);
        if (render) render(module, 1, block);
    }
    void Render(unsigned phase, std::uint32_t argument = 0) const {
        if (render) render(module, phase, argument);
    }
};
Bridge& Normal() { static Bridge bridge(1); return bridge; }
Bridge& Arrange() { static Bridge bridge(2); return bridge; }
}
// Register arguments are observed only; original guest instructions still run.
void SdojCoreNormalBuilt() { Normal().Built(); }
void SdojCoreNormalCopy(PPCRegister& block) { Normal().Copy(block.u32); }
void SdojCoreArrangeBuilt() { Arrange().Built(); }
void SdojCoreArrangeCopy(PPCRegister& block) { Arrange().Copy(block.u32); }
void SdojRenderNormalSwap() { Normal().Render(2); }
void SdojRenderNormalWaitBegin(PPCRegister& depth) { Normal().Render(3, depth.u32); }
void SdojRenderNormalWaitEnd() { Normal().Render(4); }
void SdojRenderArrangeSwap() { Arrange().Render(2); }
void SdojRenderArrangeWaitBegin(PPCRegister& depth) { Arrange().Render(3, depth.u32); }
void SdojRenderArrangeWaitEnd() { Arrange().Render(4); }
