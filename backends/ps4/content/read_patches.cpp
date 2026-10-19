#include "read_patches.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace kt::content {
namespace {
[[noreturn]] void Invalid() { throw std::runtime_error("Invalid or incompatible content read-overlay file"); }
class Reader {
public:
    explicit Reader(const std::filesystem::path& path) {
        std::ifstream source(path, std::ios::binary | std::ios::ate);
        if (!source) { Invalid(); }
        const auto size = source.tellg();
        if (size < 24 || size > 4 * 1024 * 1024) { Invalid(); }
        bytes.resize(static_cast<std::size_t>(size));
        source.seekg(0);
        if (!source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) { Invalid(); }
    }
    std::span<const std::byte> Take(std::size_t size) {
        if (size > bytes.size() - at) { Invalid(); }
        auto result = std::span<const std::byte>(bytes).subspan(at, size); at += size; return result;
    }
    std::uint64_t Number(unsigned size) {
        const auto input = Take(size); std::uint64_t result{};
        for (unsigned i = 0; i < size; ++i) { result |= std::uint64_t{std::to_integer<unsigned char>(input[i])} << (i * 8); }
        return result;
    }
    std::string Text(std::size_t size) {
        const auto input = Take(size);
        return {reinterpret_cast<const char*>(input.data()), input.size()};
    }
    bool Done() const { return at == bytes.size(); }
private:
    std::vector<std::byte> bytes;
    std::size_t at{};
};
bool Digits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}
void ValidatePath(std::string_view path) {
    if (path.empty() || path.starts_with('/') || path.ends_with('/') ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == ':' || c == '\\'; })) { Invalid(); }
    for (std::size_t begin = 0; begin < path.size();) {
        const auto slash = path.find('/', begin);
        const auto part = path.substr(begin, slash == path.npos ? slash : slash - begin);
        if (part.empty() || part == "." || part == "..") { Invalid(); }
        if (slash == path.npos) { break; }
        begin = slash + 1;
    }
}
}
ReadPatchSet ReadPatchSet::Load(const std::filesystem::path& path) {
    Reader input(path);
    if (input.Text(8) != std::string("KTFIX01\0", 8)) { Invalid(); }
    const auto count = input.Number(4), title_length = input.Number(4), version_length = input.Number(4);
    if (!count || count > 16 || title_length != 9 || version_length != 5 || input.Number(4)) { Invalid(); }
    ReadPatchSet result;
    result.title = input.Text(static_cast<std::size_t>(title_length));
    result.version = input.Text(static_cast<std::size_t>(version_length));
    if (!result.title.starts_with("CUSA") || !Digits(std::string_view(result.title).substr(4)) ||
        !Digits(std::string_view(result.version).substr(0, 2)) || result.version[2] != '.' ||
        !Digits(std::string_view(result.version).substr(3))) { Invalid(); }
    for (unsigned i = 0; i < count; ++i) {
        const auto name_length = input.Number(4), length = input.Number(4);
        ReadPatch patch;
        patch.file_size = input.Number(8); patch.offset = input.Number(8);
        if (!name_length || name_length > 256 || !length || length > 262144 || patch.file_size > INT64_MAX ||
            patch.offset > patch.file_size || length > patch.file_size - patch.offset) { Invalid(); }
        const auto hash = input.Take(patch.original_sha256.size());
        std::copy(hash.begin(), hash.end(), patch.original_sha256.begin());
        std::array<std::byte, 32> replacement_hash{};
        const auto encoded_hash = input.Take(replacement_hash.size());
        std::copy(encoded_hash.begin(), encoded_hash.end(), replacement_hash.begin());
        patch.path = input.Text(static_cast<std::size_t>(name_length)); ValidatePath(patch.path);
        const auto replacement = input.Take(static_cast<std::size_t>(length));
        if (ReadPatchHash(replacement) != replacement_hash) { Invalid(); }
        patch.replacement.assign(replacement.begin(), replacement.end());
        if (!result.entries.empty()) {
            const auto& previous = result.entries.back();
            if (patch.path < previous.path || (patch.path == previous.path &&
                (patch.file_size != previous.file_size || patch.offset < previous.offset + previous.replacement.size()))) { Invalid(); }
        }
        result.entries.push_back(std::move(patch));
    }
    if (!input.Done()) { Invalid(); }
    return result;
}
std::array<std::byte, 32> ReadPatchHash(std::span<const std::byte> bytes) {
    struct Algorithm { BCRYPT_ALG_HANDLE value{}; ~Algorithm() { if (value) { BCryptCloseAlgorithmProvider(value, 0); } } } algorithm;
    std::array<std::byte, 32> result{};
    if (bytes.size() > (std::numeric_limits<ULONG>::max)() ||
        BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptHash(algorithm.value, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
            static_cast<ULONG>(bytes.size()), reinterpret_cast<PUCHAR>(result.data()), static_cast<ULONG>(result.size())) < 0) {
        throw std::runtime_error("Cannot validate original read-overlay bytes");
    }
    return result;
}
}
