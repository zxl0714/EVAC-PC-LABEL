#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cave::ps2 {
class StateSlots {
public:
    static constexpr unsigned Count = 99;
    StateSlots(std::filesystem::path directory, std::string programIdentity);
    std::array<std::optional<std::filesystem::file_time_type>, Count> List() const;
    void Save(unsigned slot, std::span<const uint8_t> state) const;
    std::vector<uint8_t> Read(unsigned slot) const;
    struct SavedProgram {
        std::string name;
        std::vector<uint8_t> bytes;
    };
    SavedProgram ReadProgram(unsigned slot,std::span<const std::string_view> discPrograms) const;
    unsigned LastUsed() const;
    void Remember(unsigned slot) const;
private:
    std::filesystem::path directory_;
    std::string identity_;
    std::filesystem::path Path(unsigned slot) const;
};
}
