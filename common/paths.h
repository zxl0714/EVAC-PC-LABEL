#pragma once
#include "common/catalogue.h"

namespace cave {
fs::path Resolve(const fs::path& root, const fs::path& value);
bool Within(const fs::path& child, const fs::path& parent);
fs::path DataChild(const fs::path& root, std::string_view relative);
// Stable across path aliases/case and shared by per-installation coordination.
std::wstring CollectionIdentity(const fs::path& root);
struct GamePaths {
  fs::path root, data, prepared, profile, logs, cache;
};
GamePaths Paths(const fs::path& root, std::string_view game, const fs::path& data,
                std::string_view profile = "default");
void ValidateOutputs(const GamePaths& paths);
}  // namespace cave
