// ESPGaluda SLPS-25352: SCE symbol matches from the pinned analyzer.
// Boot SHA-256: ff8eaac60476e5bf3789aa5b3b972fa0f50a50b104b7cc8a1e78af042d510055.
// Keep SDK services on one runtime ABI; original SIF/CD polling otherwise waits
// on firmware-side state that is owned by these host implementations.
#include "game_overrides.h"
#include "../../backends/ps2/sdk_binding.h"
#include "ps2_runtime.h"
#include <iostream>
#include <stdexcept>
#ifdef EVAC_PS2_ESPGALUDA_MODULES
void RegisterEspgaludaStaticModules(PS2Runtime& runtime);
#endif

// Allocations share the runtime heap with kernel services and DMA helpers.
// String and math routines retain the original implementations.
// Upstream TODO services retain their original statically recompiled bodies.
namespace {
void SearchSingleLayer(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
  // This DVD has one layer. Its layer-search API uses the same file result
  // layout and first two arguments as sceCdSearchFile.
  if(_mm_cvtsi128_si32(context->r[6])!=0) {
    context->r[2]=_mm_setzero_si128();return;
  }
  ps2_stubs::sceCdSearchFile(memory,context,runtime);
}
void BindSdk(PS2Runtime& runtime) {
#ifdef EVAC_PS2_ESPGALUDA_MODULES
  RegisterEspgaludaStaticModules(runtime);
#endif
  if(!runtime.replaceFunction(0x00112090u,&cave::ps2::SdkEntry<&SearchSingleLayer>))
    throw std::runtime_error("Cannot bind ESPGaluda single-layer disc search");
  if(!cave::ps2::BindSdkHandler(runtime,0x001009a8u,"sceGsResetGraph"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsResetGraph");
  if(!cave::ps2::BindSdkHandler(runtime,0x00100b48u,"sceGsResetPath"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsResetPath");
  if(!cave::ps2::BindSdkHandler(runtime,0x00100c60u,"sceGsSetDefDispEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefDispEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00100fd8u,"sceGsPutDispEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsPutDispEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101098u,"sceGszbufaddr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGszbufaddr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101160u,"sceGsSetDefDrawEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefDrawEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101348u,"sceGsSetDefClear"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefClear");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101450u,"sceGsPutDrawEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsPutDrawEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101538u,"sceGsSetDefDBuff"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefDBuff");
  if(!cave::ps2::BindSdkHandler(runtime,0x001017d0u,"sceGsSwapDBuff"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSwapDBuff");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101830u,"sceGsSyncV"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSyncV");
  if(!cave::ps2::BindSdkHandler(runtime,0x001018c8u,"sceGsSyncPath"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSyncPath");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101be0u,"sceGsSetDefLoadImage"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefLoadImage");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101dc8u,"sceGsSetDefStoreImage"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSetDefStoreImage");
  if(!cave::ps2::BindSdkHandler(runtime,0x00101f08u,"sceGsExecLoadImage"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsExecLoadImage");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102088u,"sceGsExecStoreImage"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsExecStoreImage");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102718u,"sceGsSyncVCallback"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceGsSyncVCallback");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102998u,"sceDmaGetChan"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaGetChan");
  if(!cave::ps2::BindSdkHandler(runtime,0x001029c0u,"sceDmaReset"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102aa0u,"sceDmaPutEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaPutEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102c78u,"sceDmaGetEnv"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaGetEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102cc8u,"sceDmaSend"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaSend");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102d30u,"sceDmaSendN"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaSendN");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102da8u,"sceDmaSendI"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaSendI");
  if(!cave::ps2::BindSdkHandler(runtime,0x00102f68u,"sceDmaSync"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDmaSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x00103520u,"sceDevVif0Reset"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceDevVif0Reset");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104268u,"write"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: write");
  if(!cave::ps2::BindSdkHandler(runtime,0x001042e8u,"read"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: read");
  if(!cave::ps2::BindSdkHandler(runtime,0x001044f0u,"_sceSDC"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSDC");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104630u,"_sceIDC"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceIDC");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104a78u,"InitThread"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: InitThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104b68u,"iWakeupThread"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: iWakeupThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104c00u,"iRotateThreadReadyQueue"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: iRotateThreadReadyQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x00104de0u,"StartThread"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: StartThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106b00u,"sceSifInitCmd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifInitCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106d80u,"sceSifExitCmd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifExitCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106de8u,"sceSifAddCmdHandler"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifAddCmdHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106e60u,"sceSifRemoveCmdHandler"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRemoveCmdHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106eb0u,"_sceSifSendCmd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSifSendCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00106fe8u,"sceSifSendCmd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifSendCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107068u,"_sceSifCmdIntrHdlr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSifCmdIntrHdlr");
  if(!cave::ps2::BindSdkHandler(runtime,0x001071b0u,"sceSifWriteBackDCache"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifWriteBackDCache");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107260u,"sceSifInitRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifInitRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x001074f0u,"_sceRpcGetFPacket"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceRpcGetFPacket");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107520u,"_sceRpcGetFPacket2"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceRpcGetFPacket2");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107740u,"sceSifGetOtherData"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifGetOtherData");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107a08u,"sceSifBindRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifBindRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107be8u,"sceSifCallRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifCallRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107de8u,"sceSifCheckStatRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifCheckStatRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107e28u,"sceSifSetRpcQueue"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifSetRpcQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107ec0u,"sceSifRegisterRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRegisterRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00107f90u,"sceSifRemoveRpc"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRemoveRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00108028u,"sceSifRemoveRpcQueue"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRemoveRpcQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x001080b8u,"sceSifGetNextRequest"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifGetNextRequest");
  if(!cave::ps2::BindSdkHandler(runtime,0x00108110u,"sceSifExecRequest"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifExecRequest");
  if(!cave::ps2::BindSdkHandler(runtime,0x001082d8u,"sceSifRpcLoop"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRpcLoop");
  if(!cave::ps2::BindSdkHandler(runtime,0x001089a8u,"_sceFsSemInit"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceFsSemInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x00108b28u,"sceFsInit"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceFsInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x00108dc0u,"sceFsReset"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceFsReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x00109028u,"sceOpen"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x001092b8u,"sceClose"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x00109430u,"sceLseek"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceLseek");
  if(!cave::ps2::BindSdkHandler(runtime,0x00109670u,"sceRead"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x001098e0u,"sceWrite"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceWrite");
  if(!cave::ps2::BindSdkHandler(runtime,0x00109ba0u,"sceIoctl"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceIoctl");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010bf50u,"sceSifInitIopHeap"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifInitIopHeap");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010bfd8u,"sceSifAllocIopHeap"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifAllocIopHeap");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c048u,"sceSifAllocSysMemory"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifAllocSysMemory");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c0c8u,"sceSifFreeSysMemory"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifFreeSysMemory");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c140u,"sceSifFreeIopHeap"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifFreeIopHeap");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c160u,"sceSifLoadIopHeap"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifLoadIopHeap");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c630u,"sceSifLoadFileReset"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifLoadFileReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010c668u,"_sceSifLoadModuleBuffer"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSifLoadModuleBuffer");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010cc78u,"_sceSifLoadModule"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSifLoadModule");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010cee0u,"_sceSifLoadElfPart"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _sceSifLoadElfPart");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d008u,"sceSifLoadElf"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifLoadElf");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d030u,"sceSifGetIopAddr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifGetIopAddr");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d120u,"sceSifSetIopAddr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifSetIopAddr");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d200u,"sceSifResetIop"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifResetIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d358u,"sceSifIsAliveIop"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifIsAliveIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d380u,"sceSifSyncIop"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifSyncIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d3d0u,"sceSifRebootIop"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceSifRebootIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010d7b8u,"InitTLB"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: InitTLB");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010e4f8u,"InitAlarm"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: InitAlarm");
  if(!cave::ps2::BindSdkHandler(runtime,0x0010ff90u,"scePadInit"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110118u,"scePadInit2"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInit2");
  if(!cave::ps2::BindSdkHandler(runtime,0x001101f8u,"scePadEnd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadEnd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110398u,"scePadPortOpen"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadPortOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110580u,"scePadPortClose"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadPortClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110638u,"scePadGetDmaStr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetDmaStr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110698u,"scePadGetFrameCount"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetFrameCount");
  if(!cave::ps2::BindSdkHandler(runtime,0x001106e8u,"scePadRead"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x001107a0u,"scePadGetState"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetState");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110868u,"scePadStateIntToStr"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadStateIntToStr");
  if(!cave::ps2::BindSdkHandler(runtime,0x001108a0u,"scePadSetReqState"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetReqState");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110948u,"scePadGetReqState"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetReqState");
  if(!cave::ps2::BindSdkHandler(runtime,0x001109d0u,"scePadInfoAct"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInfoAct");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110af0u,"scePadInfoComb"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInfoComb");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110c10u,"scePadInfoMode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInfoMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110d48u,"scePadSetMainMode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetMainMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110e00u,"scePadSetActDirect"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetActDirect");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110ec0u,"scePadSetActAlign"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetActAlign");
  if(!cave::ps2::BindSdkHandler(runtime,0x00110f98u,"scePadGetButtonMask"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetButtonMask");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111050u,"scePadSetButtonInfo"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetButtonInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111100u,"scePadInfoPressMode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadInfoPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111160u,"scePadEnterPressMode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadEnterPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x001111b8u,"scePadExitPressMode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadExitPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111210u,"scePadSetVrefParam"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetVrefParam");
  if(!cave::ps2::BindSdkHandler(runtime,0x001112e0u,"scePadGetPortMax"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetPortMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111348u,"scePadGetSlotMax"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetSlotMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x001113b0u,"scePadGetModVersion"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadGetModVersion");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111418u,"scePadSetWarningLevel"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: scePadSetWarningLevel");
  // Keep the original SDR RPC client: the generic placeholder reports voice
  // transfers as complete without loading their sample data into SPU2 RAM.
  if(!cave::ps2::BindSdkHandler(runtime,0x00111a88u,"sceCdCallback"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdCallback");
  if(!cave::ps2::BindSdkHandler(runtime,0x00111c60u,"sceCdInitEeCB"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdInitEeCB");
  if(!cave::ps2::BindSdkHandler(runtime,0x00112508u,"sceCdNcmdDiskReady"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdNcmdDiskReady");
  if(!cave::ps2::BindSdkHandler(runtime,0x001125a0u,"sceCdSync"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x00112640u,"sceCdSyncS"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdSyncS");
  if(!cave::ps2::BindSdkHandler(runtime,0x001128b8u,"sceCdInit"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x00112db8u,"sceCdDiskReady"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdDiskReady");
  if(!cave::ps2::BindSdkHandler(runtime,0x00112fd8u,"sceCdGetReadPos"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdGetReadPos");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113008u,"sceCdRead"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x001131e8u,"sceCdSeek"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdSeek");
  if(!cave::ps2::BindSdkHandler(runtime,0x001132b8u,"sceCdGetError"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdGetError");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113350u,"sceCdBreak"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdBreak");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113408u,"sceCdMmode"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdMmode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113500u,"sceCdReadClock"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceCdReadClock");
  if(!cave::ps2::BindSdkHandler(runtime,0x001135c0u,"sceMcInit"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x001137e8u,"sceMcEnd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcEnd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113850u,"sceMcChangeThreadPriority"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcChangeThreadPriority");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113910u,"sceMcGetSlotMax"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcGetSlotMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x001139d0u,"sceMcOpen"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113af8u,"sceMcMkdir"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcMkdir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113b30u,"sceMcClose"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113bf0u,"sceMcSeek"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcSeek");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113cd0u,"mceIntrReadFixAlign"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: mceIntrReadFixAlign");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113d80u,"sceMcRead"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x00113e98u,"sceMcWrite"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcWrite");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114010u,"sceMcSync"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x001140d8u,"mceGetInfoApdx"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: mceGetInfoApdx");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114130u,"sceMcGetInfo"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcGetInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114320u,"sceMcGetDir"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcGetDir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114470u,"mceStorePwd"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: mceStorePwd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114500u,"sceMcChdir"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcChdir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114640u,"sceMcFormat"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcFormat");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114710u,"sceMcDelete"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcDelete");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114830u,"sceMcFlush"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcFlush");
  if(!cave::ps2::BindSdkHandler(runtime,0x001148f0u,"sceMcSetFileInfo"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcSetFileInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114ac0u,"sceMcRename"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcRename");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114c20u,"sceMcUnformat"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcUnformat");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114cf0u,"sceMcGetEntSpace"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: sceMcGetEntSpace");
  if(!cave::ps2::BindSdkHandler(runtime,0x00114e70u,"_calloc_r"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _calloc_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x001167b8u,"_free_r"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _free_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x00117330u,"memalign"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: memalign");
  if(!cave::ps2::BindSdkHandler(runtime,0x00116a98u,"_malloc_trim_r"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _malloc_trim_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x00117560u,"malloc_extend_top"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: malloc_extend_top");
  if(!cave::ps2::BindSdkHandler(runtime,0x001177b8u,"_malloc_r"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: _malloc_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x001182c0u,"__malloc_lock"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: __malloc_lock");
  if(!cave::ps2::BindSdkHandler(runtime,0x00118318u,"__malloc_unlock"))
    throw std::runtime_error("Cannot bind ESPGaluda SDK handler: __malloc_unlock");
  std::cerr<<"[ps2-bindings] ESPGaluda SDK: 158 handlers\n";
}
}
PS2_REGISTER_GAME_OVERRIDE("ESPGaluda SLPS-25352 SDK","",0x00100008u,0x95b4197du,BindSdk)
