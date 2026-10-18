#pragma once
#include "config/settings.h"
#include "common/paths.h"
#include "common/launch.h"
#include <filesystem>
#include <memory>
#include <string>

namespace espgaluda2 {
struct RunContext {
    std::filesystem::path root, profile, directory;
    Settings settings;
    std::string source_build;
    cave::GamePaths paths;
    std::shared_ptr<void> profile_lock;
    std::shared_ptr<cave::GameSession> game_session;
};
std::string Sha256File(const std::filesystem::path& file);
std::string Sha256Text(std::string_view text);
RunContext PrepareRun(const std::filesystem::path& root, const std::filesystem::path& profile = {},
                      const std::filesystem::path& data_override = {});
void RecordStartupError(const RunContext& run, std::string_view error);
void RecordEarlyStartupError(const std::filesystem::path& root,std::string_view error);
}
