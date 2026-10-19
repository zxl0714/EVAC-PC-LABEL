#pragma once
#include <stdexcept>
#include <string>
// Only the standalone memory/cache test target links this diagnostic sink.
// Production UnsupportedApi remains the fatal diagnostic from native_probe.
struct NativeMemoryTestUnsupported : std::runtime_error {
  explicit NativeMemoryTestUnsupported(const char *name)
      : std::runtime_error(name) {}
};
void AuthoredNativeMemoryFaultStore(volatile unsigned char *address);
unsigned char AuthoredNativeMemoryFaultLoad(volatile unsigned char *address);
