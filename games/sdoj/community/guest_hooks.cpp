#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/xam/community_service.h>
#include <rex/cvar.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>
#include <algorithm>

REXCVAR_DEFINE_BOOL(sdoj_native_trace,false,"Diagnostics","Observe SDOJ native community schema without HTTP")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

void SdojTraceView(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                    PPCRegister& r6,PPCRegister& r7,PPCRegister& r8) {
  if(REXCVAR_GET(sdoj_native_trace))
    REXLOG_INFO("SDOJ_NATIVE view object={:08X} view={} rows={} write_fields={} read_columns={} kind={}",
        r3.u32,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32);
}
void SdojTraceColumn(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if(REXCVAR_GET(sdoj_native_trace))
    REXLOG_INFO("SDOJ_NATIVE column object={:08X} index={} column={:04X}",r3.u32,r4.u32,r5.u32);
}
void SdojTraceRating(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if(REXCVAR_GET(sdoj_native_trace))
    REXLOG_INFO("SDOJ_NATIVE rating object={:08X} rating={} kind={}",r3.u32,r4.s64,r5.u32);
}
void SdojTraceProperty64(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,PPCRegister& r6) {
  if(REXCVAR_GET(sdoj_native_trace))
    REXLOG_INFO("SDOJ_NATIVE property object={:08X} index={} property={:08X} type=2 value={}",
        r3.u32,r4.u32,r5.u32,r6.s64);
}
void SdojTraceProperty32(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,PPCRegister& r6) {
  if(REXCVAR_GET(sdoj_native_trace))
    REXLOG_INFO("SDOJ_NATIVE property object={:08X} index={} property={:08X} type=1 value={}",
        r3.u32,r4.u32,r5.u32,r6.s32);
}
void SdojTraceRecording(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5) {
  if(!REXCVAR_GET(sdoj_native_trace))return;
  std::string header;
  const auto size=std::min(r5.u32,128u),address=r4.u32;
  auto* memory=rex::Runtime::instance()->memory();
  auto* heap=memory->LookupHeap(address);
  if(size && address && uint64_t(address)+size<=0x100000000ull && heap
      && heap==memory->LookupHeap(address+size-1)
      && (uint32_t(heap->QueryRangeAccess(address,address+size-1))&1)) {
    const auto* data=memory->TranslateVirtual(address);
    constexpr char digits[]="0123456789abcdef";
    for(uint32_t i=0;i<size;++i) {header+=digits[data[i]>>4];header+=digits[data[i]&15];}
  }
  REXLOG_INFO("SDOJ_NATIVE recording object={:08X} bytes={} header={}",r3.u32,r5.u32,header);
}

// Only statically linked Xbox API boundaries. Original callers decide which
// scores qualify, which views they affect, and whether to share the recording.
void SdojCommunityEstimateRank(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                              PPCRegister& r6,PPCRegister& r7) {
  r3.u64=rex::system::xam::StartCommunityRankIo(rex::Runtime::instance()->kernel_state(),
      r3.u32,r4.u32,r5.u32,r6.u32,r7.u32);
}
void SdojStorageUpload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                        PPCRegister& r6,PPCRegister& r7) {
  r3.u64=rex::system::xam::StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),
      true,r3.u32,r4.u32,r5.u32,r6.u32,0,0,r7.u32);
}
void SdojStorageDownload(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                          PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::StartCommunityReplayIo(rex::Runtime::instance()->kernel_state(),
      false,r3.u32,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void SdojStorageLocalPath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                         PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),
      r3.u32,0,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
void SdojStorageRemotePath(PPCRegister& r3,PPCRegister& r4,PPCRegister& r5,
                          PPCRegister& r6,PPCRegister& r7,PPCRegister& r8,PPCRegister& r9) {
  r3.u64=rex::system::xam::BuildCommunityReplayPath(rex::Runtime::instance()->kernel_state(),
      254,r3.u64,r4.u32,r5.u32,r6.u32,r7.u32,r8.u32,r9.u32);
}
