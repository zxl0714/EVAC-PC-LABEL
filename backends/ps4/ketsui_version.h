#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Independently verified CUSA09062 executable contracts. These are metadata,
// not an offset heuristic: never apply one version's sites to another image.
// The loaded prepared image retains the original executable SHA-256 identity.
struct KetsuiVersion {
    std::string_view version, source;
    std::size_t archive_seed_load, factory, factory_slot, score_table;
    std::array<std::size_t,3> job_targets;
    std::array<std::size_t,2> backend_slots, backend_targets;
    std::size_t psb_root, psb_has, psb_item, psb_integer, copy_profile;
    std::size_t module_loaded, context, service_label;
    std::uint32_t replay_data_version;
    std::array<std::size_t,3> JobSlots() const { return {score_table+0x40,score_table,score_table+8}; }
};
inline constexpr std::array<KetsuiVersion,2> kKetsuiVersions{{
    {"01.00","22927d470a261d52c31510b8e5f173f4f6256307828e302f6eab19f4357c0e8f",
     0xd2,0x1e4a20,0x2d8e88,0x2d8f68,{0x1e7330,0x1e8d30,0x1e8d40},
     {0x2d8e68,0x2d8e70},{0x1e40a0,0x1e4410},
     0x1b0d10,0x1b2e30,0x1b4000,0x1b0f40,0x1fdad0,0x791ac8,0x791acc,0x2e0f38,0x10000},
    {"01.02","a32b4b798cc09d8b3f756acde33d679dbb7fec1a54eec50cc179f3f3694ac073",
     0xd3,0x1e4ca0,0x2d8ed8,0x2d8fb8,{0x1e75b0,0x1e8fb0,0x1e8fc0},
     {0x2d8eb8,0x2d8ec0},{0x1e4320,0x1e4690},
     0x1b0f90,0x1b30b0,0x1b4280,0x1b11c0,0x1fdd50,0x791ac8,0x791acc,0x2e0f38,0x10001}
}};
inline const KetsuiVersion* FindKetsuiVersion(std::string_view source) {
    for(const auto& version:kKetsuiVersions)if(version.source==source)return &version;
    return nullptr;
}
inline const KetsuiVersion* FindKetsuiVersion(std::span<const std::uint8_t,32> source) {
    std::string digest;digest.reserve(64);
    for(auto byte:source) {digest+="0123456789abcdef"[byte>>4];digest+="0123456789abcdef"[byte&15];}
    return FindKetsuiVersion(digest);
}
