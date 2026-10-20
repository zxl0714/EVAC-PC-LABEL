#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include "input/keyboard_config.h"

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
void ConfigureNativePadSettings(const kt::input::KeyboardBindings& bindings, bool gamepad, double deadzone);
bool ConfigureNativeDiagnosticTaps(unsigned milliseconds);
// Opt-in developer entry only: H holds Circle for this duration (0..2000 ms);
// Shift+H holds it for at most 30 s, normal H shortens it, focus loss clears it.
bool ConfigureNativeDiagnosticCircleHold(unsigned milliseconds);
bool PadSelfTest();
int PadDemo();
