#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ps2_stubs {
std::vector<uint8_t> saveMpegState();
void loadMpegState(std::span<const uint8_t> bytes);
void resetMpegStubState();
std::vector<uint8_t> saveSdkState();
void loadSdkState(std::span<const uint8_t> bytes);
std::vector<uint8_t> saveMemoryCardState();
void loadMemoryCardState(std::span<const uint8_t> bytes);
}
