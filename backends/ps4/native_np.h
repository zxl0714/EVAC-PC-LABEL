#pragma once
#include <cstdint>
#include <string_view>
#include <filesystem>
#include <memory>

namespace kt::community { class Client; }
// Explicit development-only startup attachment. The caller restricts it to
// literal HTTP loopback; ordinary startup never installs this provider yet.
bool ConfigureNativeCommunity(std::shared_ptr<kt::community::Client> client);
enum class CommunityScoreModuleAction { Load, Unload, IsLoaded };
int CommunityScoreModule(CommunityScoreModuleAction action);
bool CommunityStateSmoke(const std::filesystem::path& config, const std::filesystem::path& directory);

std::uint64_t ResolveNpImport(std::string_view identifier, std::string_view library, std::string_view module);
bool NpSelfTest();
bool NpRequestSelfTest();
