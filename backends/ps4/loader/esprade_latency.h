#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace cave::ps4 {
inline constexpr std::array<std::string_view,5> kEspradeLatencyPrograms{
        "59d3aa99b902eade4e2069a6c778a3a442715be4816aca951c4f300f82880d69",
        "46fb512a30b680784a0f792556a86ced58305fcff4475610c10dd892bc72c10e",
        "9c8d0a7f77e983b1379d2f469c2e510e8345e3f1668b642a2600b3255da13396",
        "0896b435c78ae9438c95d88927961f90b241c16a845dce9d376ccb40da063a18",
        "96d673c91afe2ecc03c45ae316cb10573f1007627fa9c279af4d912585e64953"};
inline bool EspradeLatencyModule(std::string_view hash) {
    return std::ranges::find(kEspradeLatencyPrograms,hash)!=kEspradeLatencyPrograms.end();
}
inline bool EspradeLatencyModule(const std::array<std::uint8_t,32>& hash) {
    constexpr std::string_view hex="0123456789abcdef";
    return std::ranges::any_of(kEspradeLatencyPrograms,[&](auto identity) {
        for (std::size_t i=0;i<hash.size();++i)
            if (identity[i*2]!=hex[hash[i]>>4] || identity[i*2+1]!=hex[hash[i]&15]) return false;
        return true;
    });
}
// Only the local prepared copy changes. Keep the original machine query and
// disabled branch: when low latency is ON, retain EAX=2 instead of subtracting
// the base-machine carry. The original save/predict/restore implementation runs.
inline bool ApplyEspradeLatency(std::span<std::uint8_t> executable, std::uint64_t address,
                               const std::array<std::uint8_t,32>& hash,std::string_view verified_equivalent={}) {
    if (!EspradeLatencyModule(hash) && !EspradeLatencyModule(verified_equivalent)) return false;
    constexpr std::uint64_t site=0x10ef;
    constexpr std::array<std::uint8_t,11> original{0x83,0xf8,0x01,0xb8,0x02,0,0,0,0x83,0xd8,0};
    if (site<address || site-address>=executable.size()) return false;
    const auto offset=static_cast<std::size_t>(site-address);
    if (original.size()>executable.size()-offset ||
        !std::equal(original.begin(),original.end(),executable.begin()+offset))
        throw std::runtime_error("ESP Ra.De. low-latency selector differs from its verified module");
    std::fill_n(executable.begin()+offset+8,3,std::uint8_t{0x90});
    return true;
}
}
