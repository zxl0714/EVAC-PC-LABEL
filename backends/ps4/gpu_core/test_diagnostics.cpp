#include "test_diagnostics.h"
#include "../native_diagnostics.h"
[[noreturn]] void UnsupportedApi(const char *name, std::uint64_t,
                                 const void *) {
  throw NativeMemoryTestUnsupported(name);
}
void TraceNativeApi(const char *, std::uint64_t, const void *) {}
__declspec(noinline) void
AuthoredNativeMemoryFaultStore(volatile unsigned char *address) {
  *address = 1;
}
__declspec(noinline) unsigned char
AuthoredNativeMemoryFaultLoad(volatile unsigned char *address) {
  return *address;
}
