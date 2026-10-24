#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace guwange {
inline constexpr uint32_t kReplayHeaderBytes=256;
inline constexpr uint32_t kReplayFixedPayloadBytes=0x2cab8;
// Community transport policy, not a claimed limit of the original recorder.
inline constexpr uint32_t kReplayMaxBytes=8*1024*1024;
inline constexpr auto kReplayFormat="guwange-base-native-v1";
struct ReplayPlayer { uint32_t character,score; };
struct ReplayInfo {
  uint64_t filetime;
  uint32_t mode,version,payload_bytes;
  int32_t stage;
  std::array<ReplayPlayer,2> players;
};
ReplayInfo InspectReplay(std::span<const uint8_t> record);
}
