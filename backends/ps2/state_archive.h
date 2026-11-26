#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cave::ps2 {
// Fixed-width little-endian fields. Structures must enumerate their guest
// fields explicitly so compiler padding and host pointers never reach disk.
static_assert(std::endian::native == std::endian::little);
class StateWriter {
public:
    explicit StateWriter(std::vector<uint8_t>& output) : output_(output) {}
    void Bytes(std::span<const uint8_t> bytes) {
        output_.insert(output_.end(), bytes.begin(), bytes.end());
    }
    template<class T> requires std::is_integral_v<T>
    void Field(T value) {
        if constexpr (std::is_same_v<T, bool>) output_.push_back(value ? 1 : 0);
        else Bytes({reinterpret_cast<const uint8_t*>(&value), sizeof(value)});
    }
    template<class... T> void Fields(const T&... values) { (Field(values), ...); }
    void Field(const float& value) { Field(std::bit_cast<uint32_t>(value)); }
    void Blob(std::span<const uint8_t> bytes) {
        if (bytes.size() > UINT32_MAX) throw std::length_error("PS2 state block too large");
        Field(static_cast<uint32_t>(bytes.size()));
        Bytes(bytes);
    }
    template<class Container, class Visit>
    void Items(Container& items, uint32_t maximum, size_t, Visit visit) {
        if (items.size() > maximum) throw std::length_error("Too many PS2 state entries");
        Field(static_cast<uint32_t>(items.size()));
        for (auto& item : items) visit(item);
    }
    void String(std::string_view value) {
        if (value.size() > UINT32_MAX) throw std::length_error("PS2 state string too long");
        Field(static_cast<uint32_t>(value.size()));
        Bytes({reinterpret_cast<const uint8_t*>(value.data()), value.size()});
    }
private:
    std::vector<uint8_t>& output_;
};

class StateReader {
public:
    explicit StateReader(std::span<const uint8_t> input) : input_(input) {}
    std::span<const uint8_t> Take(size_t size) {
        if (size > input_.size()) throw std::invalid_argument("Truncated PS2 state");
        auto bytes = input_.first(size);
        input_ = input_.subspan(size);
        return bytes;
    }
    void Bytes(std::span<uint8_t> target) {
        const auto bytes = Take(target.size());
        if (!target.empty()) std::memcpy(target.data(), bytes.data(), target.size());
    }
    template<class T> requires std::is_integral_v<T>
    void Field(T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            uint8_t flag{};
            Field(flag);
            if (flag > 1) throw std::invalid_argument("Invalid boolean in PS2 state");
            value = flag != 0;
        } else Bytes({reinterpret_cast<uint8_t*>(&value), sizeof(value)});
    }
    template<class... T> void Fields(T&... values) { (Field(values), ...); }
    void Field(float& value) {
        uint32_t bits{};
        Field(bits);
        value = std::bit_cast<float>(bits);
    }
    uint32_t Count(uint32_t maximum, size_t bytesPerEntry) {
        uint32_t count{};
        Field(count);
        if (count > maximum || count > input_.size() / bytesPerEntry)
            throw std::invalid_argument("Invalid PS2 state entry count");
        return count;
    }
    std::span<const uint8_t> Blob(uint32_t maximum = UINT32_MAX) {
        return Take(Count(maximum, 1));
    }
    void Blob(std::vector<uint8_t>& result) {
        const auto bytes = Blob();
        result.assign(bytes.begin(), bytes.end());
    }
    template<class Container, class Visit>
    void Items(Container& items, uint32_t maximum, size_t minimumBytes, Visit visit) {
        const auto count = Count(maximum, minimumBytes);
        items.clear();
        for (uint32_t i = 0; i < count; ++i) visit(items.emplace_back());
    }
    void Finish() const {
        if (!input_.empty()) throw std::invalid_argument("Unexpected trailing PS2 state data");
    }
    std::string String(uint32_t maximum) {
        const auto bytes = Take(Count(maximum, 1));
        if (bytes.empty()) return {};
        std::string result(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (result.find('\0') != std::string::npos) throw std::invalid_argument("Invalid PS2 state string");
        return result;
    }
private:
    std::span<const uint8_t> input_;
};
}
