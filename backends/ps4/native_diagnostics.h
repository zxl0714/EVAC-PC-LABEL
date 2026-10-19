#pragma once
#include <cstdint>

void SetNativeErrno(int value);
[[noreturn]] void UnsupportedApi(const char* name, std::uint64_t detail, const void* caller);
// A known user close is separate from an API frontier. It remains an incomplete
// stop (27), not a successful graceful shutdown or a completed presentation.
[[noreturn]] void StopForNativeWindowClose(const char* stage, unsigned video, unsigned buffer, const void* caller);
void TraceNativeApi(const char* name, std::uint64_t value, const void* caller);
// True only inside an executable segment of a published game image.
bool NativeExecutableAddress(std::uintptr_t address);
