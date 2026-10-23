#include "protocol.h"
#include "common/nickname.h"
#include "../config/ini_sections.h"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace kt::community {
namespace {
void Require(bool okay, const char* error) { if (!okay) { throw std::runtime_error(error); } }
std::wstring Wide(std::string_view text) {
    Require(!text.empty() && text.size() <= 16384, "Invalid UTF-8 extent");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    Require(size > 0, "Invalid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size) == size, "Invalid UTF-8");
    return result;
}
std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == text.npos) { return {}; }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
std::array<std::byte, 32> Digest(std::string_view text) {
    BCRYPT_ALG_HANDLE algorithm{};
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA-256 provider unavailable");
    struct Close { BCRYPT_ALG_HANDLE value; ~Close() { BCryptCloseAlgorithmProvider(value, 0); } } close{algorithm};
    std::array<std::byte, 32> result{};
    Require(text.size() <= 0xffffffffu && BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), static_cast<ULONG>(text.size()),
        reinterpret_cast<PUCHAR>(result.data()), static_cast<ULONG>(result.size())) >= 0, "SHA-256 failed");
    return result;
}
std::string Hex(std::span<const std::byte> data) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (const auto value : data) {
        const auto byte = std::to_integer<unsigned>(value);
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}
}

bool ValidNicknameBytes(std::string_view value) {
    if (value.empty() || value.size() > 256 || value.find('\0') != value.npos) { return false; }
    try { (void)Wide(value); } catch (...) { return false; }
    // Canonical Unicode category/count validation belongs to the shared service.
    return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool Identifier(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
const TitleProtocol& ProtocolFor(std::string_view game) {
    static constexpr TitleProtocol ketsui{"ketsui","CUSA09062","1.02","ketsui-npscore-1.02"};
    static constexpr TitleProtocol daioujou{"daioujou","CUSA32997","1.04","daioujou-npscore-1.04"};
    static constexpr TitleProtocol esprade{"esprade","CUSA15155","1.04","esprade-npscore-1.04"};
    if (game==ketsui.game) return ketsui;
    if (game==daioujou.game) return daioujou;
    if (game==esprade.game) return esprade;
    throw std::runtime_error("Unknown PS4 community game");
}
Endpoint ParseEndpoint(std::string_view url,std::string_view game) {
    (void)ProtocolFor(game);
    Require(url.size() <= 1024 && url.find_first_of("\r\n\t\\") == url.npos && url.find('\0') == url.npos, "Invalid community endpoint");
    const auto wide = Wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    Require(WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) &&
            parts.dwHostNameLength && parts.dwHostNameLength <= 253 && !parts.dwUserNameLength &&
            !parts.dwPasswordLength && !parts.dwExtraInfoLength && parts.nPort != 0, "Invalid community endpoint");
    Endpoint result{std::wstring(parts.lpszHostName, parts.dwHostNameLength),
                    std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength), parts.nPort,
                    parts.nScheme == INTERNET_SCHEME_HTTPS};
    Require(result.secure || (parts.nScheme == INTERNET_SCHEME_HTTP && result.host == L"127.0.0.1"), "Community requires HTTPS except literal loopback");
    const auto expected=std::wstring(L"/cave/api/v1/games/")+Wide(game);
    const bool legacy=game=="ketsui" && (result.path==L"/ketsui/api/v1" || result.path==L"/ketsui/api/v1/");
    if (result.path==expected+L"/" || (legacy && result.path.ends_with(L"/"))) result.path.pop_back();
    Require(legacy || result.path==expected,"Community endpoint must use this game's protocol path");
    return result;
}
Config ParseConfig(std::string_view text,std::string_view game) {
    (void)ProtocolFor(game);
    const auto sections = config::SplitSections(text);
    Require(sections.has_community, "Community config requires a community section");
    text = sections.community;
    Config result;
    result.game=game;
    if (game!="ketsui") {
        result.endpoint="https://xiaolu.life/cave/api/v1/games/"+std::string(game);
        result.nickname="Player";
    }
    bool section{};
    std::set<std::string> keys;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = Trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') { continue; }
        if (line.front() == '[') {
            Require(!section && line == "[community]", "Unknown or repeated community section");
            section = true; continue;
        }
        const auto equal = line.find('=');
        Require(section && equal != line.npos, "Invalid community config line");
        const auto key = Trim(line.substr(0, equal)), value = Trim(line.substr(equal + 1));
        Require(keys.emplace(key).second, "Duplicate community config key");
        if (key == "enabled") {
            Require(value == "true" || value == "false", "Community enabled must be true or false");
            result.enabled = value == "true";
        } else if (key == "nickname") { result.nickname = value; }
        else if (key == "endpoint") { result.endpoint = value; }
        else { throw std::runtime_error("Unknown community config key"); }
    }
    Require(section && keys.contains("enabled"), "Community config requires an explicit enabled field");
    (void)ParseEndpoint(result.endpoint,game);
    Require(!result.enabled || ValidNicknameBytes(result.nickname), "Enabled community config requires a UTF-8 nickname");
    return result;
}
Config LoadConfig(const std::filesystem::path& path,std::string_view game) {
    Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= config::kMaximumConfigBytes, "Community config is missing or too large");
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "Cannot read community config");
    std::array<char, config::kMaximumConfigBytes + 1> text{};
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    Require(!input.bad() && input.gcount() <= static_cast<std::streamsize>(config::kMaximumConfigBytes), "Community config changed or failed during read");
    return ParseConfig(std::string_view(text.data(), static_cast<std::size_t>(input.gcount())),game);
}
Json ParseJson(std::string_view text, std::size_t limit) {
    Require(text.size() <= limit, "Community JSON exceeds limit");
    std::array<std::set<std::string>, 18> keys;
    std::size_t key_count{};
    auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        Require(depth >= 0 && depth <= 16, "Community JSON nesting exceeds limit");
        const auto index = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) { keys[index + 1].clear(); }
        if (event == Json::parse_event_t::key) {
            Require(++key_count <= 8192 && keys[index].emplace(parsed.get<std::string>()).second, "Duplicate or excessive community JSON keys");
        }
        return true;
    };
    auto value = Json::parse(text.begin(), text.end(), callback, false);
    Require(!value.is_discarded(), "Invalid community JSON");
    return value;
}
std::string Base64(std::span<const std::byte> data) {
    if (data.empty()) { return {}; }
    Require(data.size() <= kBodyLimit, "Base64 source exceeds limit");
    DWORD size{};
    Require(CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size), "Base64 encoding failed");
    std::string result(size, '\0');
    Require(CryptBinaryToStringA(reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &size), "Base64 encoding failed");
    result.resize(size && result[size - 1] == '\0' ? size - 1 : size);
    return result;
}
std::vector<std::byte> Unbase64(std::string_view text, std::size_t limit) {
    Require(limit <= kBodyLimit && text.size() <= ((limit + 2) / 3) * 4, "Base64 extent exceeds limit");
    if (text.empty()) { return {}; }
    DWORD size{};
    Require(CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
                                 nullptr, &size, nullptr, nullptr) && size <= limit, "Invalid Base64");
    std::vector<std::byte> result(size);
    Require(CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(result.data()), &size, nullptr, nullptr), "Invalid Base64");
    Require(Base64(result) == text, "Noncanonical Base64");
    return result;
}
std::string Sha256(std::span<const std::byte> data) { return Sha256(std::string_view(reinterpret_cast<const char*>(data.data()), data.size())); }
std::string Sha256(std::string_view data) { const auto digest = Digest(data); return Hex(digest); }
std::uint64_t AccountId(std::string_view name,std::string_view game) {
    std::string input(ProtocolFor(game).game);
    input += "-community-v1";input.push_back('\0');
    input += name;
    const auto hash = Digest(input);
    std::uint64_t result{};
    std::memcpy(&result, hash.data(), sizeof(result));
    result &= 0x7fffffffffffffffull;
    return result ? result : 1;
}
std::uint64_t Decimal(std::string_view text, std::uint64_t maximum) {
    Require(!text.empty() && text.size() <= 20 && (text.size() == 1 || text.front() != '0') &&
        std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }), "Invalid decimal field");
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value <= maximum, "Decimal field exceeds bounds");
    return value;
}
std::string NewId() {
    std::array<std::byte, 16> bytes{};
    Require(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "Community random source unavailable");
    return Hex(bytes);
}
std::string ShortOnlineId(std::string_view name) {
    Require(ValidNicknameBytes(name), "Invalid display name");
    return cave::Ps4DisplayNickname(name);
}
}
