#pragma once
#include "common/settings.h"
#include "common/paths.h"
#include "common/launch.h"
#include <filesystem>
#include <memory>
#include <string>

namespace cave::xbox360 {
struct BuildIdentity {
    std::string_view game, commit, dirty, program_metadata;
};
struct RunContext {
    std::filesystem::path root, profile, directory, content_source;
    std::vector<cave::ContentInspection> content;
    std::string game;
    Settings settings;
    std::string source_build;
    cave::GamePaths paths;
    std::shared_ptr<void> profile_lock;
    std::shared_ptr<cave::GameSession> game_session;
};
std::string Sha256File(const std::filesystem::path& file);
std::string Sha256Text(std::string_view text);
RunContext PrepareRun(const std::filesystem::path& root, const BuildIdentity& build,
                      const std::filesystem::path& profile = {},
                      const std::filesystem::path& data_override = {});
void RecordStartupError(const RunContext& run, std::string_view error);
void RecordEarlyStartupError(const std::filesystem::path& root,std::string_view game,std::string_view error);
}
