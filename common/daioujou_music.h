#pragma once
#include "common/paths.h"
#include <functional>

namespace cave::daioujou {
struct ArrangeCue {
  std::string_view name;
  uint32_t samples, loop_start, loop_length;
};

std::span<const ArrangeCue> ArrangeCues();
fs::path ArrangeMusicDirectory(const fs::path& collection);
bool HasArrangeMusic(const fs::path& collection);
void ImportArrangeMusic(const fs::path& collection, const fs::path& iso,
                        const std::function<void(uint64_t,uint64_t)>& progress={},
                        const std::function<bool()>& cancelled={});
}
