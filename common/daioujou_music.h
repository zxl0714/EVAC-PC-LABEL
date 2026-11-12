#pragma once
#include "common/paths.h"
#include <functional>

namespace cave::daioujou {
struct ArrangeCue {
  std::string_view name;
  uint32_t samples, loop_start, loop_length;
};
struct ArrangeBank { std::string_view name; uint64_t bytes; std::string_view sha256; };
enum class ArrangeState { Absent, Ready, Missing, Invalid };
struct ArrangeStatus {
  ArrangeState state{ArrangeState::Absent};
  std::string file, detail;
  bool Problem() const { return state==ArrangeState::Missing || state==ArrangeState::Invalid; }
};
std::span<const ArrangeCue> ArrangeCues();
std::span<const ArrangeBank> ArrangeBanks();
// data is the selected PS4 game directory, including custom external folders.
ArrangeStatus InspectArrangeMusic(const fs::path& data,CheckLevel level,VerificationCache* cache=nullptr);
// Emit the verified WAV bytes directly into preparation; no user-managed WAVs.
void ReadArrangeWaves(const fs::path& data,
    const std::function<void(std::string_view,std::span<const uint8_t>)>& consume,
    VerificationCache* cache=nullptr);
}
