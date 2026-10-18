#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
// Shared with a separate dump writer; no handles or C++ objects.
struct CrashRecord {
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
    char game[32];
    char build[64];
};
