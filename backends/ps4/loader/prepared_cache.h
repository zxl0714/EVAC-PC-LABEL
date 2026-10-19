#pragma once
#include "preparation_io.h"
#include <functional>
#include <nlohmann/json.hpp>

namespace cave::ps4 {
struct PreparedCache {
    fs::path directory;
    std::string key,miss_reason;
    bool reused{};
    nlohmann::json receipt;
};
// A generation is immutable once published. Invalid generations and failed
// staging directories are preserved, never selected as a fallback or deleted.
PreparedCache EnsurePreparedCache(const fs::path& root,const fs::path& data,
    const nlohmann::json& recipe,std::span<const std::string> files,
    const std::function<void(const fs::path&)>& build);
void WritePreparedRelative(const fs::path& stage,std::string_view name,std::span<const std::uint8_t> bytes);
struct PreparedGame {
    PreparedCache cache;
    fs::path executable,modules,read_fixes;
};
PreparedGame PrepareKetsuiGame(const fs::path& root,const fs::path& data);
int PrepareGameCommand(int argc,wchar_t** argv);
}
