#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct FutariReplaySlot {
  uint32_t index;
  uint32_t offset;
  uint32_t size;
};

constexpr uint32_t kFutariReplaySlots = 99;
constexpr uint32_t kFutariReplayTableBytes = kFutariReplaySlots * 8;
constexpr uint32_t kFutariReplayVersion = 0x09100800;
constexpr uint32_t kFutariMaxReplayBytes = 8 * 1024 * 1024;
constexpr uint32_t kFutariStorageReplayHeaderBytes = 104;
constexpr uint32_t kFutariStorageReplayMaxBytes = 0xd30b8;

// Throws on malformed, overlapping, truncated or incompatible input. The
// native save contains 99 slots. Share only the selected slot, not other runs.
std::vector<FutariReplaySlot> FutariReadReplaySlots(std::span<const uint8_t> data);
std::vector<uint8_t> FutariExtractReplay(std::span<const uint8_t> data, uint32_t slot);
std::vector<uint8_t> FutariWrapReplay(std::span<const uint8_t> record);

// XStorage uses a separate native envelope: total bytes at +0, version at +4,
// then title metadata and the original (possibly compressed) stream at +104.
// This is not a slot from replaydata.bin; never wrap it in the local save table.
void FutariValidateStorageReplay(std::span<const uint8_t> record);
