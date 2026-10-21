#include <rex/ppc.h>
#include <windows.h>

// The hook is compiled into both overlapping modules, but only CA022100 has
// the reviewed tilemap site. A disabled init-only capture has no host call.
void SdojLogicNormalTilemap(PPCContext& ctx,std::uint8_t*) {
    if(ctx.r21.u32!=9)return; // One observation before the nine-row loop.
    using Sink=void (*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,
                       std::uint32_t,std::uint32_t,std::uint32_t);
    static const auto sink=[]() -> Sink {
        const auto host=GetModuleHandleW(nullptr);
        using Enabled=bool (*)();
        const auto enabled=reinterpret_cast<Enabled>(GetProcAddress(host,"SdojLogicContentEnabled"));
        return enabled && enabled()
            ? reinterpret_cast<Sink>(GetProcAddress(host,"SdojLogicTilemapEvent")) : nullptr;
    }();
    if(sink)sink(ctx.r22.u32,ctx.r25.u32,ctx.r23.u32,ctx.r24.u32,
                 ctx.r27.u32,ctx.r6.u32,std::uint32_t(ctx.lr));
}
