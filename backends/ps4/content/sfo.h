#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kt::content {
struct SfoValue { std::uint16_t format; std::vector<std::byte> data; };
class Sfo {
public:
    static Sfo Parse(std::span<const std::byte> bytes);
    std::optional<std::int32_t> Integer(std::string_view key) const;
    std::optional<std::string> Text(std::string_view key) const;
    std::optional<std::vector<std::byte>> Binary(std::string_view key) const;
    void SetInteger(std::string_view key, std::int32_t value);
    void SetText(std::string_view key, std::string_view value);
    void SetBinary(std::string_view key, std::span<const std::byte> value);
    std::vector<std::byte> Serialize() const;
    void Overlay(const Sfo& patch);
private:
    std::map<std::string, SfoValue, std::less<>> values_;
};
bool SfoSelfTest();
}
