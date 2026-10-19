#pragma once
#include <chrono>
#include <string_view>

// Start only after collection stdout redirection. Standalone diagnostic commands
// retain synchronous output unless they explicitly start the queue themselves.
bool StartNativeAsyncLog();
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
int NativeLogf(const char* format, ...) noexcept;
bool WriteNativeLog(std::string_view text) noexcept;
bool StopNativeAsyncLog(std::chrono::milliseconds timeout = std::chrono::seconds(2)) noexcept;
// Direct output: call after draining, before a final terminal record.
void PrintNativeAsyncLogDiagnostics() noexcept;
bool NativeAsyncLogSelfTest();
