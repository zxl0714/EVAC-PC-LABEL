#include "replay_format.h"
#include <algorithm>
#include <stdexcept>

namespace {
uint32_t Read32(std::span<const uint8_t> data, size_t offset) {
  return (uint32_t(data[offset]) << 24) | (uint32_t(data[offset+1]) << 16)
       | (uint32_t(data[offset+2]) << 8) | data[offset+3];
}
void Write32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
  for (size_t i=0;i<4;++i) data[offset+i] = uint8_t(value >> ((3-i)*8));
}
void ValidateRecord(std::span<const uint8_t> record) {
  if (record.size() < 0xa0 || record.size() > kFutariMaxReplayBytes-kFutariReplayTableBytes
      || Read32(record, 0) != kFutariReplayVersion || Read32(record, 12) != kFutariReplayVersion
      || Read32(record, 8) != record.size()-4)
    throw std::runtime_error("Incompatible or damaged Futari replay record");
}
}

std::vector<FutariReplaySlot> FutariReadReplaySlots(std::span<const uint8_t> data) {
  if (data.size() < kFutariReplayTableBytes || data.size() > kFutariMaxReplayBytes)
    throw std::runtime_error("Invalid Futari replay size");
  std::vector<FutariReplaySlot> slots;
  uint32_t cursor = kFutariReplayTableBytes;
  for (uint32_t i=0;i<kFutariReplaySlots;++i) {
    const auto offset = Read32(data, i*8);
    const auto size = Read32(data, i*8+4);
    if (offset != cursor || size > data.size()-cursor)
      throw std::runtime_error("Invalid Futari replay slot table");
    if (size) {
      ValidateRecord(data.subspan(offset, size));
      slots.push_back({i, offset, size});
      cursor += size;
    }
  }
  if (cursor != data.size()) throw std::runtime_error("Trailing bytes in Futari replay");
  return slots;
}

std::vector<uint8_t> FutariWrapReplay(std::span<const uint8_t> record) {
  ValidateRecord(record);
  std::vector<uint8_t> result(kFutariReplayTableBytes + record.size());
  for (uint32_t i=0;i<kFutariReplaySlots;++i) {
    Write32(result, i*8, i == 0 ? kFutariReplayTableBytes : uint32_t(result.size()));
    Write32(result, i*8+4, i == 0 ? uint32_t(record.size()) : 0);
  }
  std::copy(record.begin(), record.end(), result.begin()+kFutariReplayTableBytes);
  return result;
}

std::vector<uint8_t> FutariExtractReplay(std::span<const uint8_t> data, uint32_t slot) {
  const auto slots = FutariReadReplaySlots(data);
  auto it = std::find_if(slots.begin(), slots.end(), [slot](const auto& s) { return s.index == slot; });
  if (it == slots.end()) throw std::runtime_error("Empty or missing Futari replay slot");
  return FutariWrapReplay(data.subspan(it->offset, it->size));
}

void FutariValidateStorageReplay(std::span<const uint8_t> record) {
  if(record.size()<=kFutariStorageReplayHeaderBytes || record.size()>kFutariStorageReplayMaxBytes
      || Read32(record,0)!=record.size() || Read32(record,4)!=kFutariReplayVersion)
    throw std::runtime_error("Incompatible or damaged Futari XStorage replay envelope");
}
