#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

enum class CaptureResult { Saved, NotLoaded, Disabled, InvalidFrame, Failed };
enum class CaptureModuleAction { Load, Unload, IsLoaded };

int ScreenshotModule(CaptureModuleAction action);
std::uint64_t ResolveScreenshotImport(std::string_view identifier, std::string_view library,
                                      std::string_view module);
// The renderer will supply an owned, top-down BGRA8 readback. This function does
// not read the desktop or manufacture a frame when no renderer frame exists.
CaptureResult ExportScreenshot(const std::filesystem::path& target, unsigned width, unsigned height,
                               unsigned stride, std::span<const std::uint8_t> bgra) noexcept;
bool ScreenshotSelfTest(const std::filesystem::path& output_directory);
