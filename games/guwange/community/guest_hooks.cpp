#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/xam/community_service.h>

// Base-version static Xbox API boundaries. Keep the original title's score,
// rank eligibility, confirmation, recording and completion state machines.
void GuwangeCommunityEstimateRank(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                                  PPCRegister& r6,PPCRegister& r7) {
  r3.u64=rex::system::xam::StartCommunityRankIo(rex::Runtime::instance()->kernel_state(),
      r3.u32,r4.u32,r5.u32,r6.u32,r7.u32);
}
void GuwangeStorageUpload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                          PPCRegister& r6,PPCRegister& r7) {
  r3.u64=rex::system::xam::StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),
      true,r3.u32,r4.u32,r5.u32,r6.u32,0,0,r7.u32);
}
void GuwangeStorageDownload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                            PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),
      false,r3.u32,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void GuwangeStorageLocalPath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                             PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),
      r3.u32,0,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void GuwangeStorageRemotePath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                              PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),
      254,r3.u64,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
