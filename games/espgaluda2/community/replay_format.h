#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace espgaluda2 {
inline constexpr uint32_t kReplayHeaderBytes = 104;
inline constexpr uint32_t kReplayMaxBytes = 8 * 1024 * 1024;
inline constexpr auto kReplayFormat = "espgaluda2-native-v2";
struct ReplayInfo {
  uint64_t time;
  int64_t score;
  uint32_t mode, character;
};
// This is not Futari's version/length convention or its local save table.
ReplayInfo InspectReplay(std::span<const uint8_t> record);
// Bind the archived tag without an Xbox Live profile lookup on local reload.
void SetDownloadedReplayAuthor(std::vector<uint8_t>& record,std::string_view native_tag);
std::string ReplaySha256(std::span<const uint8_t> record);
std::string ReplayBase64(std::span<const uint8_t> record);
std::vector<uint8_t> ReplayUnbase64(const std::string& text);
}  // namespace espgaluda2
