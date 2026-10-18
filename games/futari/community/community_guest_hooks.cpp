#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/xam/community_service.h>

// Replace only the native XStorage API boundary. Guest replay recording,
// selection, decoding and playback remain the original title's implementation.
using namespace rex::system::xam;
void FutariCommunityReplayPrepare(PPCRegister& object,PPCRegister& view,PPCRegister& size) {
  REXLOG_INFO("Community native replay task prepare: object={:08X} view={} bytes={}",object.u32,view.u32,size.u32);
}
void FutariCommunityReplayTask(PPCRegister& object) {
  REXLOG_INFO("Community native replay task start: object={:08X}",object.u32);
}
void FutariCommunityEstimateRank(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
    PPCRegister& r6,PPCRegister& r7) {
  r3.u64=StartCommunityRankIo(rex::Runtime::instance()->kernel_state(),r3.u32,r4.u32,
      r5.u32,r6.u32,r7.u32);
}
void FutariCommunityReplayPath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
    PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),r3.u32,0,
      r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void FutariCommunityReplayRemotePath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
    PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),254,r3.u64,
      r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void FutariCommunityReplayUpload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
    PPCRegister& r6,PPCRegister& r7) {
  r3.u64=StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),true,r3.u32,
      r4.u32,r5.u32,r6.u32,0,0,r7.u32);
}
void FutariCommunityReplayDownload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
    PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),false,r3.u32,
      r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
