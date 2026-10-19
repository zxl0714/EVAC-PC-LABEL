#pragma once
#include "../native_memory.h"
#include "write_tracker.h"
#include <exception>
#include <stdexcept>

namespace kt::memory {
// Authored self-test data only. Exercise the public SysV direct-memory APIs,
// their real global lifecycle registry, and two aliases of the same section.
class DirectFixture {
    using Capacity = std::uint64_t(__attribute__((sysv_abi))*)();
    using Allocate = int(__attribute__((sysv_abi))*)(std::int64_t, std::int64_t, std::uint64_t, std::uint64_t, int, std::int64_t*);
    using Map = int(__attribute__((sysv_abi))*)(void**, std::uint64_t, int, int, std::int64_t, std::uint64_t);
    using Release = int(__attribute__((sysv_abi))*)(std::uint64_t, std::uint64_t);
    using Protect = int(__attribute__((sysv_abi))*)(void*, std::uint64_t, int);
    std::int64_t physical_ = -1;
    Release release_ = reinterpret_cast<Release>(ResolveMemoryImport("MBuItvba6z8"));
public:
    static constexpr std::uint64_t bytes = 65536;
    void *first{}, *alias{};
    DirectFixture() {
        const auto allocate = reinterpret_cast<Allocate>(ResolveMemoryImport("rTXw65xmLIA"));
        const auto map = reinterpret_cast<Map>(ResolveMemoryImport("L-Q3LEjIbgA"));
        const auto capacity = reinterpret_cast<Capacity>(ResolveMemoryImport("pO96TwzOm5E"))();
        if (allocate(0, static_cast<std::int64_t>(capacity), bytes, bytes, 0, &physical_)) { throw std::runtime_error("Cannot allocate authored direct-memory fixture"); }
        if (map(&first, bytes, 3, 0, physical_, bytes) || map(&alias, bytes, 3, 0, physical_, bytes)) {
            release_(static_cast<std::uint64_t>(physical_), bytes); throw std::runtime_error("Cannot map authored direct-memory aliases");
        }
    }
    ~DirectFixture() { Reset(); }
    DirectFixture(const DirectFixture&) = delete;
    DirectFixture& operator=(const DirectFixture&) = delete;
    void Reset() {
        if (physical_ < 0) { return; }
        if (release_(static_cast<std::uint64_t>(physical_), bytes)) { std::terminate(); }
        physical_ = -1; first = alias = nullptr;
    }
    bool ProtectAlias(int protection) {
        return reinterpret_cast<Protect>(ResolveMemoryImport("vSMAm3cxYTY"))(alias, bytes, protection) == 0;
    }
    WriteStamp Track(std::uint64_t extent = 4096) { return TrackNativeMemoryRange(first, extent); }
};
}
