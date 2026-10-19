#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

std::uint64_t ResolvePadImport(std::string_view identifier, std::string_view library, std::string_view module);
// Called only by the runtime-owned window's message loop. No global key polling.
void NativePadAttachWindow(void* window);
void NativePadDetachWindow(void* window);
void NativePadWindowFocus(void* window, bool focused);
void NativePadWindowKey(void* window, unsigned key, bool down);
// Optional physical input binding; configured before the window/pad starts.
bool ConfigureNativePadMouseLeft(std::wstring_view button);
bool NativePadMouseLeftMapped();
// Read/validate fully before publishing; only before pad/window initialization.
void ConfigureNativePadKeyboard(const std::filesystem::path& path);
bool ConfigureNativeDiagnosticTaps(unsigned milliseconds);
bool ConfigureNativeDiagnosticCircleHold(unsigned milliseconds);
bool PadSelfTest();
int PadDemo();
