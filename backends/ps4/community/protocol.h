#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace kt::community {
using Json = nlohmann::json;
inline constexpr std::size_t kReplayLimit = 8 * 1024 * 1024;
inline constexpr std::size_t kBodyLimit = ((kReplayLimit + 2) / 3) * 4 + 4096;
inline constexpr std::size_t kJsonReplyLimit = 256 * 1024;
inline constexpr std::string_view kApi = "/ketsui/api/v1";
inline constexpr std::string_view kReplayFormat = "ketsui-npscore-1.02";
inline constexpr std::string_view kDefaultEndpoint = "https://xiaolu.life/ketsui/api/v1";
inline constexpr std::string_view kDefaultNickname = "Colonel-404";
struct Config {
    bool enabled = true;
    std::string nickname = std::string(kDefaultNickname);
    std::string endpoint = std::string(kDefaultEndpoint);
};
struct Endpoint {
    std::wstring host, path;
    std::uint16_t port{};
    bool secure{};
};
Config ParseConfig(std::string_view text);
Config LoadConfig(const std::filesystem::path& path);
Endpoint ParseEndpoint(std::string_view url,std::string_view game="ketsui");
Json ParseJson(std::string_view text, std::size_t limit = kJsonReplyLimit);
std::string Base64(std::span<const std::byte> data);
std::vector<std::byte> Unbase64(std::string_view text, std::size_t limit);
std::string Sha256(std::span<const std::byte> data);
std::string Sha256(std::string_view data);
std::uint64_t AccountId(std::string_view canonical_name);
std::string NewId();
bool Identifier(std::string_view value);
bool ValidNicknameBytes(std::string_view value);
std::string ShortOnlineId(std::string_view full_name);
std::uint64_t Decimal(std::string_view text, std::uint64_t maximum = 0x7fffffffffffffffull);
}
