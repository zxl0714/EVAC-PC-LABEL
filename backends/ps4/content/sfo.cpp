#include "sfo.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <array>
#include <cstdio>

namespace kt::content {
namespace {
[[noreturn]] void Invalid() { throw std::runtime_error("Invalid or unsupported PSF metadata"); }
std::uint32_t Read(std::span<const std::byte> bytes, std::size_t offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) { Invalid(); }
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) { value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8); }
    return value;
}
bool Utf8(std::span<const std::byte> bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = std::to_integer<unsigned>(bytes[i++]);
        if (first < 128) { continue; }
        unsigned count = 0, value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 0x10000; }
        else { return false; }
        if (count > bytes.size() - i) { return false; }
        while (count--) {
            const auto next = std::to_integer<unsigned>(bytes[i++]);
            if ((next & 0xc0) != 0x80) { return false; }
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) { return false; }
    }
    return true;
}
void Key(std::string_view key) {
    if (key.empty() || key.size() > 255) { Invalid(); }
    for (const unsigned char c : key) { if (c < 33 || c > 126) { Invalid(); } }
}
void Write(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value, unsigned size) {
    for (unsigned index = 0; index < size; ++index) { bytes[offset + index] = static_cast<std::byte>(value >> (index * 8)); }
}
}
Sfo Sfo::Parse(std::span<const std::byte> bytes) {
    if (bytes.size() < 20 || bytes.size() > 1024 * 1024 || Read(bytes, 0, 4) != 0x46535000) { Invalid(); }
    const auto version = Read(bytes, 4, 4), keys = Read(bytes, 8, 4), data = Read(bytes, 12, 4), count = Read(bytes, 16, 4);
    if ((version != 0x100 && version != 0x101) || count > 4096 || keys < 20ull + count * 16ull ||
        keys > data || data > bytes.size()) { Invalid(); }
    Sfo result;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (std::size_t i = 0; i < count; ++i) {
        const auto entry = 20 + i * 16;
        const auto key = static_cast<std::size_t>(keys) + Read(bytes, entry, 2);
        const auto format = Read(bytes, entry + 2, 2), length = Read(bytes, entry + 4, 4), capacity = Read(bytes, entry + 8, 4);
        const auto value = static_cast<std::size_t>(data) + Read(bytes, entry + 12, 4);
        if (key >= data || length > capacity || value > bytes.size() || capacity > bytes.size() - value) { Invalid(); }
        auto end = key;
        while (end < data && bytes[end] != std::byte{}) { ++end; }
        if (end == key || end == data || end - key > 255) { Invalid(); }
        std::string name(reinterpret_cast<const char*>(bytes.data() + key), end - key);
        for (const unsigned char c : name) { if (c < 33 || c > 126) { Invalid(); } }
        const auto contents = bytes.subspan(value, length);
        if (format == 0x404) { if (length != 4 || capacity != 4) { Invalid(); } }
        else if (format == 0x204) {
            if (contents.empty() || contents.back() != std::byte{} || !Utf8(contents)) { Invalid(); }
            const auto zero = std::find(contents.begin(), contents.end(), std::byte{});
            if (std::any_of(zero, contents.end(), [](auto b) { return b != std::byte{}; })) { Invalid(); }
        } else if (format != 4) { Invalid(); }
        if (capacity) { ranges.emplace_back(value, value + capacity); }
        if (!result.values_.emplace(std::move(name), SfoValue{static_cast<std::uint16_t>(format), {contents.begin(), contents.end()}}).second) { Invalid(); }
    }
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i) { if (ranges[i].first < ranges[i - 1].second) { Invalid(); } }
    return result;
}
std::optional<std::int32_t> Sfo::Integer(std::string_view key) const {
    const auto found = values_.find(key);
    if (found == values_.end() || found->second.format != 0x404) { return {}; }
    return static_cast<std::int32_t>(Read(found->second.data, 0, 4));
}
std::optional<std::string> Sfo::Text(std::string_view key) const {
    const auto found = values_.find(key);
    if (found == values_.end() || found->second.format != 0x204) { return {}; }
    const auto& data = found->second.data;
    const auto end = std::find(data.begin(), data.end(), std::byte{});
    return std::string(reinterpret_cast<const char*>(data.data()), static_cast<std::size_t>(end - data.begin()));
}
std::optional<std::vector<std::byte>> Sfo::Binary(std::string_view key) const {
    const auto found = values_.find(key);
    if (found == values_.end() || found->second.format != 4) { return {}; }
    return found->second.data;
}
void Sfo::SetInteger(std::string_view key, std::int32_t value) {
    Key(key); std::vector<std::byte> bytes(4); Write(bytes, 0, static_cast<std::uint32_t>(value), 4);
    values_.insert_or_assign(std::string(key), SfoValue{0x404, std::move(bytes)});
}
void Sfo::SetText(std::string_view key, std::string_view value) {
    Key(key);
    const auto input = std::as_bytes(std::span{value});
    if (value.size() >= 1024 * 1024 || value.find('\0') != std::string_view::npos || !Utf8(input)) { Invalid(); }
    std::vector<std::byte> bytes(input.begin(), input.end()); bytes.push_back(std::byte{});
    values_.insert_or_assign(std::string(key), SfoValue{0x204, std::move(bytes)});
}
void Sfo::SetBinary(std::string_view key, std::span<const std::byte> value) {
    Key(key); if (value.size() > 1024 * 1024) { Invalid(); }
    values_.insert_or_assign(std::string(key), SfoValue{4, {value.begin(), value.end()}});
}
std::vector<std::byte> Sfo::Serialize() const {
    if (values_.size() > 4096) { Invalid(); }
    const auto keys = 20 + values_.size() * 16;
    std::size_t key_bytes{}, data_bytes{};
    for (const auto& [key, value] : values_) {
        Key(key); if (key_bytes > UINT16_MAX || value.data.size() > 1024 * 1024) { Invalid(); }
        key_bytes += key.size() + 1;
        data_bytes = (data_bytes + 3) & ~std::size_t{3}; data_bytes += value.data.size();
        if (data_bytes > 1024 * 1024) { Invalid(); }
    }
    const auto data = (keys + key_bytes + 3) & ~std::size_t{3};
    if (data + data_bytes > 1024 * 1024) { Invalid(); }
    std::vector<std::byte> bytes(data + data_bytes);
    Write(bytes, 0, 0x46535000, 4); Write(bytes, 4, 0x101, 4);
    Write(bytes, 8, static_cast<unsigned>(keys), 4); Write(bytes, 12, static_cast<unsigned>(data), 4);
    Write(bytes, 16, static_cast<unsigned>(values_.size()), 4);
    std::size_t index{}, key_offset{}, data_offset{};
    for (const auto& [key, value] : values_) {
        const auto entry = 20 + index++ * 16; data_offset = (data_offset + 3) & ~std::size_t{3};
        Write(bytes, entry, static_cast<unsigned>(key_offset), 2); Write(bytes, entry + 2, value.format, 2);
        Write(bytes, entry + 4, static_cast<unsigned>(value.data.size()), 4); Write(bytes, entry + 8, static_cast<unsigned>(value.data.size()), 4);
        Write(bytes, entry + 12, static_cast<unsigned>(data_offset), 4);
        std::memcpy(bytes.data() + keys + key_offset, key.data(), key.size());
        std::copy(value.data.begin(), value.data.end(), bytes.begin() + data + data_offset);
        key_offset += key.size() + 1; data_offset += value.data.size();
    }
    return bytes;
}
void Sfo::Overlay(const Sfo& patch) { for (const auto& [key, value] : patch.values_) { values_.insert_or_assign(key, value); } }
bool SfoSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        passed &= result; std::printf("{\"event\":\"sfo_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    Sfo metadata; const std::array<std::byte, 8> blocks{std::byte{96}};
    metadata.SetText("MAINTITLE", "Synthetic save"); metadata.SetInteger("SAVEDATA_LIST_PARAM", -7);
    metadata.SetBinary("SAVEDATA_BLOCKS", blocks); metadata.SetBinary("EMPTY", {});
    const auto bytes = metadata.Serialize(); const auto parsed = Sfo::Parse(bytes);
    check("psf_round_trip_preserves_text_signed_integer_and_binary", parsed.Text("MAINTITLE") == "Synthetic save" &&
        parsed.Integer("SAVEDATA_LIST_PARAM") == -7 && parsed.Binary("SAVEDATA_BLOCKS") == std::vector<std::byte>(blocks.begin(), blocks.end()));
    check("empty_binary_and_mismatched_types_are_distinct_from_absence", parsed.Binary("EMPTY").has_value() && parsed.Binary("EMPTY")->empty() &&
        !parsed.Binary("MAINTITLE") && !parsed.Text("SAVEDATA_BLOCKS") && !parsed.Integer("ABSENT"));
    auto changed = parsed; changed.SetText("MAINTITLE", "Replacement");
    check("metadata_update_preserves_unknown_keys_without_mutating_source", Sfo::Parse(changed.Serialize()).Binary("EMPTY").has_value() &&
        parsed.Text("MAINTITLE") == "Synthetic save" && changed.Text("MAINTITLE") == "Replacement");
    check("serialization_is_deterministic_and_aligned", bytes == metadata.Serialize() && Read(bytes, 12, 4) % 4 == 0 && Sfo::Parse(bytes).Serialize() == bytes);
    const auto rejects = [](auto function) { try { function(); return false; } catch (const std::runtime_error&) { return true; } };
    check("invalid_utf8_embedded_nul_and_key_rejected_transactionally", rejects([&] { changed.SetText("MAINTITLE", std::string_view("a\0b", 3)); }) &&
        rejects([&] { changed.SetText("MAINTITLE", "\xc0\x80"); }) && rejects([&] { changed.SetInteger("BAD KEY", 1); }) && changed.Text("MAINTITLE") == "Replacement");
    Sfo excessive; excessive.SetBinary("BIG", std::vector<std::byte>(1024 * 1024));
    check("serialization_bound_includes_headers_keys_and_padding", rejects([&] { (void)excessive.Serialize(); }));
    check("empty_metadata_round_trips", Sfo::Parse(Sfo{}.Serialize()).Serialize() == Sfo{}.Serialize());
    return passed;
}
}
