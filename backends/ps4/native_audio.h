#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveAudioImport(std::string_view identifier, std::string_view library, std::string_view module);
bool AudioSelfTest();
// PC master gain is separate from guest per-port/per-channel volume.
void ConfigureNativeAudioVolume(double volume);
