// Ibara SLPM-66301, boot SHA-256:
// 263d3079dee66af82815219351553075340a4d9f42aceaa7bb8ef2bfb812ffac.
// SCE signature matches from the pinned analyzer plus original syscall veneers.
// These services reuse the same runtime ABI as ESPGaluda. Original string,
// math, unsupported library code and game logic remain statically recompiled.
#include "game_overrides.h"
#include "../../backends/ps2/sdk_binding.h"
#include <iostream>
#include <stdexcept>
namespace {
void BindSdk(PS2Runtime& runtime) {
  if(!cave::ps2::BindSdkHandler(runtime,0x001474c8u,"sceGsGetGParam"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsGetGParam");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146c48u,"sceDmaGetChan"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaGetChan");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146c70u,"sceDmaReset"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146d50u,"sceDmaPutEnv"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaPutEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146f28u,"sceDmaGetEnv"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaGetEnv");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146f78u,"sceDmaSend"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaSend");
  if(!cave::ps2::BindSdkHandler(runtime,0x00146fe0u,"sceDmaSendN"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaSendN");
  if(!cave::ps2::BindSdkHandler(runtime,0x00147058u,"sceDmaSendI"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaSendI");
  if(!cave::ps2::BindSdkHandler(runtime,0x00147218u,"sceDmaSync"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceDmaSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x00147338u,"sceGsResetGraph"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsResetGraph");
  if(!cave::ps2::BindSdkHandler(runtime,0x001474d8u,"sceGsResetPath"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsResetPath");
  if(!cave::ps2::BindSdkHandler(runtime,0x001481c0u,"sceGsSyncV"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsSyncV");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148258u,"sceGsSyncPath"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsSyncPath");
  if(!cave::ps2::BindSdkHandler(runtime,0x001488f8u,"sceGsSyncVCallback"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceGsSyncVCallback");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148aa0u,"SetGsCrt"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SetGsCrt");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148b80u,"AddIntcHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: AddIntcHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ba0u,"RemoveIntcHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: RemoveIntcHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148bb0u,"AddDmacHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: AddDmacHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148bc0u,"AddDmacHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: AddDmacHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148bd0u,"RemoveDmacHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: RemoveDmacHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148be0u,"EnableIntc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: EnableIntc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148bf0u,"DisableIntc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: DisableIntc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c00u,"EnableDmac"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: EnableDmac");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c10u,"DisableDmac"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: DisableDmac");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c20u,"SetAlarm"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SetAlarm");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c30u,"ReleaseAlarm"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: ReleaseAlarm");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c40u,"iEnableIntc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iEnableIntc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c50u,"iDisableIntc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iDisableIntc");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c60u,"iEnableDmac"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iEnableDmac");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c70u,"iDisableDmac"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iDisableDmac");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148c80u,"iSetAlarm"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iSetAlarm");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ca0u,"CreateThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: CreateThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148cb0u,"DeleteThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: DeleteThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148cc0u,"StartThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: StartThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ce0u,"ExitDeleteThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: ExitDeleteThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148cf0u,"TerminateThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: TerminateThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148d30u,"ChangeThreadPriority"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: ChangeThreadPriority");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148d50u,"RotateThreadReadyQueue"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: RotateThreadReadyQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148d70u,"ReleaseWaitThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: ReleaseWaitThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148d90u,"GetThreadId"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: GetThreadId");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148da0u,"ReferThreadStatus"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: ReferThreadStatus");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148dc0u,"SleepThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SleepThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148dd0u,"WakeupThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: WakeupThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148de0u,"iWakeupThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iWakeupThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148e10u,"SuspendThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SuspendThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148e80u,"EndOfHeap"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: EndOfHeap");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ea0u,"CreateSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: CreateSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148eb0u,"DeleteSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: DeleteSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ec0u,"SignalSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SignalSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ed0u,"iSignalSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iSignalSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ee0u,"WaitSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: WaitSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148ef0u,"PollSema"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: PollSema");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148f40u,"SetOsdConfigParam"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SetOsdConfigParam");
  if(!cave::ps2::BindSdkHandler(runtime,0x00148f50u,"GetOsdConfigParam"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: GetOsdConfigParam");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149120u,"FlushCache"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: FlushCache");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149170u,"sceSifStopDma"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifStopDma");
  if(!cave::ps2::BindSdkHandler(runtime,0x001491c0u,"GsGetIMR"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: GsGetIMR");
  if(!cave::ps2::BindSdkHandler(runtime,0x001491e0u,"GsPutIMR"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: GsPutIMR");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149210u,"SetVSyncFlag"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: SetVSyncFlag");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149240u,"sceSifDmaStat"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifDmaStat");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149260u,"sceSifSetDma"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSetDma");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149270u,"isceSifSetDma"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: isceSifSetDma");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149280u,"sceSifSetDChain"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSetDChain");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149290u,"isceSifSetDChain"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: isceSifSetDChain");
  if(!cave::ps2::BindSdkHandler(runtime,0x001492a0u,"sceSifSetReg"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSetReg");
  if(!cave::ps2::BindSdkHandler(runtime,0x001492b0u,"sceSifGetReg"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifGetReg");
  if(!cave::ps2::BindSdkHandler(runtime,0x001492d0u,"Deci2Call"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: Deci2Call");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149300u,"GetMemorySize"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: GetMemorySize");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149310u,"InitTLB"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: InitTLB");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149468u,"write"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: write");
  if(!cave::ps2::BindSdkHandler(runtime,0x001494e8u,"read"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: read");
  if(!cave::ps2::BindSdkHandler(runtime,0x001496f0u,"_sceSDC"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSDC");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149830u,"_sceIDC"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceIDC");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149c78u,"InitThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: InitThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149d50u,"iWakeupThread"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iWakeupThread");
  if(!cave::ps2::BindSdkHandler(runtime,0x00149de8u,"iRotateThreadReadyQueue"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: iRotateThreadReadyQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b018u,"sceSifInitCmd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifInitCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b298u,"sceSifExitCmd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifExitCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b300u,"sceSifAddCmdHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifAddCmdHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b330u,"sceSifRemoveCmdHandler"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRemoveCmdHandler");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b358u,"_sceSifSendCmd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSifSendCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b490u,"sceSifSendCmd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSendCmd");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b510u,"_sceSifCmdIntrHdlr"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSifCmdIntrHdlr");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b638u,"sceSifWriteBackDCache"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifWriteBackDCache");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b6e8u,"sceSifInitRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifInitRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b978u,"_sceRpcGetFPacket"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceRpcGetFPacket");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014b9a8u,"_sceRpcGetFPacket2"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceRpcGetFPacket2");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014bbd0u,"sceSifGetOtherData"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifGetOtherData");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014be28u,"sceSifBindRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifBindRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014bff8u,"sceSifCallRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifCallRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c1e8u,"sceSifCheckStatRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifCheckStatRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c228u,"sceSifSetRpcQueue"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSetRpcQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c2c0u,"sceSifRegisterRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRegisterRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c390u,"sceSifRemoveRpc"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRemoveRpc");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c428u,"sceSifRemoveRpcQueue"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRemoveRpcQueue");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c4b8u,"sceSifGetNextRequest"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifGetNextRequest");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c510u,"sceSifExecRequest"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifExecRequest");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014c6d8u,"sceSifRpcLoop"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRpcLoop");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014cc18u,"_sceFsSemInit"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceFsSemInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014cd70u,"sceFsInit"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceFsInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d008u,"sceFsReset"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceFsReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d040u,"sceOpen"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d2c8u,"sceClose"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d448u,"sceLseek"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceLseek");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d680u,"sceRead"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014d8e0u,"sceWrite"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceWrite");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014dba0u,"sceIoctl"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceIoctl");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014ff88u,"sceSifLoadFileReset"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifLoadFileReset");
  if(!cave::ps2::BindSdkHandler(runtime,0x0014ffc0u,"_sceSifLoadModuleBuffer"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSifLoadModuleBuffer");
  if(!cave::ps2::BindSdkHandler(runtime,0x001505d0u,"_sceSifLoadModule"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSifLoadModule");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150838u,"_sceSifLoadElfPart"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _sceSifLoadElfPart");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150960u,"sceSifLoadElf"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifLoadElf");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150988u,"sceSifGetIopAddr"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifGetIopAddr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150a78u,"sceSifSetIopAddr"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSetIopAddr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150b58u,"sceSifResetIop"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifResetIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150c98u,"sceSifIsAliveIop"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifIsAliveIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150cc0u,"sceSifSyncIop"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifSyncIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x00150cf8u,"sceSifRebootIop"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceSifRebootIop");
  if(!cave::ps2::BindSdkHandler(runtime,0x001510e0u,"InitTLB"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: InitTLB");
  if(!cave::ps2::BindSdkHandler(runtime,0x00151df0u,"InitAlarm"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: InitAlarm");
  if(!cave::ps2::BindSdkHandler(runtime,0x00151f58u,"sceCdCallback"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdCallback");
  if(!cave::ps2::BindSdkHandler(runtime,0x00152130u,"sceCdInitEeCB"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdInitEeCB");
  if(!cave::ps2::BindSdkHandler(runtime,0x00152a08u,"sceCdNcmdDiskReady"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdNcmdDiskReady");
  if(!cave::ps2::BindSdkHandler(runtime,0x00152aa0u,"sceCdSync"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x00152b40u,"sceCdSyncS"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdSyncS");
  if(!cave::ps2::BindSdkHandler(runtime,0x00152dd0u,"sceCdInit"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x001532a8u,"sceCdDiskReady"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdDiskReady");
  if(!cave::ps2::BindSdkHandler(runtime,0x001534b0u,"sceCdMmode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceCdMmode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153698u,"scePadInit"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x001537d8u,"scePadInit2"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInit2");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153878u,"scePadEnd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadEnd");
  if(!cave::ps2::BindSdkHandler(runtime,0x001538f8u,"scePadPortOpen"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadPortOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153ae0u,"scePadPortClose"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadPortClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153b98u,"scePadGetDmaStr"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetDmaStr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153bf8u,"scePadGetFrameCount"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetFrameCount");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153c48u,"scePadRead"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153db0u,"scePadGetState"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetState");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153e28u,"scePadStateIntToStr"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadStateIntToStr");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153e60u,"scePadSetReqState"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetReqState");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153ec8u,"scePadGetReqState"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetReqState");
  if(!cave::ps2::BindSdkHandler(runtime,0x00153f50u,"scePadInfoAct"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInfoAct");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154070u,"scePadInfoComb"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInfoComb");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154190u,"scePadInfoMode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInfoMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x001542c8u,"scePadSetMainMode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetMainMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154380u,"scePadSetActDirect"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetActDirect");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154440u,"scePadSetActAlign"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetActAlign");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154518u,"scePadGetButtonMask"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetButtonMask");
  if(!cave::ps2::BindSdkHandler(runtime,0x001545d0u,"scePadSetButtonInfo"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetButtonInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154680u,"scePadInfoPressMode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadInfoPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x001546e0u,"scePadEnterPressMode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadEnterPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154738u,"scePadExitPressMode"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadExitPressMode");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154790u,"scePadSetVrefParam"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetVrefParam");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154860u,"scePadGetPortMax"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetPortMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x001548c8u,"scePadGetSlotMax"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetSlotMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154930u,"scePadGetModVersion"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadGetModVersion");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154998u,"scePadSetWarningLevel"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: scePadSetWarningLevel");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154a00u,"sceMcInit"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcInit");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154bd0u,"sceMcEnd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcEnd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154c50u,"sceMcChangeThreadPriority"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcChangeThreadPriority");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154d08u,"sceMcGetSlotMax"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcGetSlotMax");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154dd0u,"sceMcOpen"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcOpen");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154ef8u,"sceMcMkdir"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcMkdir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154f30u,"sceMcClose"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcClose");
  if(!cave::ps2::BindSdkHandler(runtime,0x00154fe8u,"sceMcSeek"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcSeek");
  if(!cave::ps2::BindSdkHandler(runtime,0x001550c0u,"mceIntrReadFixAlign"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: mceIntrReadFixAlign");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155150u,"sceMcRead"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcRead");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155268u,"sceMcWrite"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcWrite");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155420u,"sceMcSync"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcSync");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155500u,"mceGetInfoApdx"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: mceGetInfoApdx");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155558u,"sceMcGetInfo"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcGetInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155748u,"sceMcGetDir"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcGetDir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155890u,"mceStorePwd"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: mceStorePwd");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155918u,"sceMcChdir"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcChdir");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155a58u,"sceMcFormat"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcFormat");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155b28u,"sceMcDelete"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcDelete");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155c40u,"sceMcFlush"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcFlush");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155cf8u,"sceMcSetFileInfo"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcSetFileInfo");
  if(!cave::ps2::BindSdkHandler(runtime,0x00155ec0u,"sceMcRename"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcRename");
  if(!cave::ps2::BindSdkHandler(runtime,0x00156018u,"sceMcUnformat"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcUnformat");
  if(!cave::ps2::BindSdkHandler(runtime,0x001560e8u,"sceMcGetEntSpace"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: sceMcGetEntSpace");
  if(!cave::ps2::BindSdkHandler(runtime,0x00157480u,"_calloc_r"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _calloc_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x00158e48u,"_free_r"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _free_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x00159140u,"_malloc_trim_r"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _malloc_trim_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x0015a2b0u,"malloc_extend_top"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: malloc_extend_top");
  if(!cave::ps2::BindSdkHandler(runtime,0x0015a508u,"_malloc_r"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _malloc_r");
  if(!cave::ps2::BindSdkHandler(runtime,0x0015b058u,"__malloc_lock"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: __malloc_lock");
  if(!cave::ps2::BindSdkHandler(runtime,0x0015b0d8u,"__malloc_unlock"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: __malloc_unlock");
  // Both aligned-allocation entry points must use the same host allocator as
  // _malloc_r/_free_r; the guest implementation edits newlib chunk headers.
  if(!cave::ps2::BindSdkHandler(runtime,0x00159fe0u,"memalign"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: memalign");
  if(!cave::ps2::BindSdkHandler(runtime,0x0015a048u,"_memalign_r"))
    throw std::runtime_error("Cannot bind Ibara SDK handler: _memalign_r");
  std::cerr<<"[ps2-bindings] Ibara SDK ready\n";
}
}
PS2_REGISTER_GAME_OVERRIDE("Ibara SLPM-66301 SDK","",0x00100008u,0xf9085060u,BindSdk)
