#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

#if defined(FUTARI_SCOPED_AUDIO_WAIT_FIX)
inline constexpr char kFutariBuild[] = "FutariPC-20261015-audio-r3";
#elif defined(FUTARI_AUDIO_DIAGNOSTIC_BUILD)
inline constexpr char kFutariBuild[] = "FutariPC-20261015-audio-r2";
#else
inline constexpr char kFutariBuild[] = "FutariPC-20261015-modes-r3";
#endif
// Shared with a separate, non-debugger process. No handles or C++ objects here.
struct FutariCrashRecord {
  volatile LONG claimed;
  DWORD thread_id;
  EXCEPTION_RECORD exception;
  CONTEXT context;
  wchar_t report_prefix[4096];
  char reason[32];
  char module_path[512];
  char phase[32];
  uint64_t guest_memory_base;
  uint32_t guest_image_base;
  uint32_t guest_image_size;
};
