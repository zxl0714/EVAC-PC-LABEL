#pragma once
#include "community/client.h"
#include <memory>
#include <string_view>

std::uint64_t ResolveScoreImport(std::string_view identifier, std::string_view library, std::string_view module);
// Startup-only attachment. Does not change NP state or publish module 0x1e.
// The replay bridge and native menu integration must be ready before that gate opens.
bool ConfigureNativeScore(std::shared_ptr<kt::community::Client> client);
bool NativeScoreContextsLive();
bool ScoreSelfTest();
bool ScoreReplaySelfTest();
bool ScoreSmoke(const std::filesystem::path& config, const std::filesystem::path& directory);
bool ScoreReplaySmoke(const std::filesystem::path& config, const std::filesystem::path& directory,
                      const std::filesystem::path& payload);
