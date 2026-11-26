#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Independently verified Japanese and US executable contracts. These are metadata,
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
inline constexpr std::array<KetsuiVersion,5> kKetsuiVersions{{
    {"01.00","22927d470a261d52c31510b8e5f173f4f6256307828e302f6eab19f4357c0e8f",
     0xd2,0x1e4a20,0x2d8e88,0x2d8f68,{0x1e7330,0x1e8d30,0x1e8d40},
     {0x2d8e68,0x2d8e70},{0x1e40a0,0x1e4410},
     0x1b0d10,0x1b2e30,0x1b4000,0x1b0f40,0x1fdad0,0x791ac8,0x791acc,0x2e0f38,0x10000},
    {"01.02","a32b4b798cc09d8b3f756acde33d679dbb7fec1a54eec50cc179f3f3694ac073",
     0xd3,0x1e4ca0,0x2d8ed8,0x2d8fb8,{0x1e75b0,0x1e8fb0,0x1e8fc0},
     {0x2d8eb8,0x2d8ec0},{0x1e4320,0x1e4690},
     0x1b0f90,0x1b30b0,0x1b4280,0x1b11c0,0x1fdd50,0x791ac8,0x791acc,0x2e0f38,0x10001},
    // Plaintext SELF repack of 01.00 (package revision 01.03, 2020). Its decoded
    // ELF is byte-identical to the 01.00 image above, so the sites are the same.
    {"01.00","9113b52b4b61c71cf557a74f9d9e3dc29af91f572c245cdec5ba09c7e1b0a8ed",
     0xd2,0x1e4a20,0x2d8e88,0x2d8f68,{0x1e7330,0x1e8d30,0x1e8d40},
     {0x2d8e68,0x2d8e70},{0x1e40a0,0x1e4410},
     0x1b0d10,0x1b2e30,0x1b4000,0x1b0f40,0x1fdad0,0x791ac8,0x791acc,0x2e0f38,0x10000},
    // 01.03 has new code, not a wrapper of 01.02. Function landmarks,
    // relocation targets and context accesses were verified independently.
    {"01.03","7ab773375a6c71853b0777f25edb5c4cf40c4b93cdbfb872392206d41c3b96df",
     0xd3,0x1e5220,0x2d8ed8,0x2d8fb8,{0x1e7b30,0x1e9530,0x1e9540},
     {0x2d8eb8,0x2d8ec0},{0x1e48a0,0x1e4c10},
     0x1b1650,0x1b3770,0x1b4940,0x1b1880,0x1fe2e0,0x791ac8,0x791acc,0x2e0f38,0x10001},
    // CUSA17078 ships the revised replay format in the base US application.
    {"01.00","f4499dec819ea5c98882c5fbbc76fb86909737184f980955ae3d6eac788bc85c",
     0xd2,0x1e5070,0x2d8ea8,0x2d8f88,{0x1e7980,0x1e9380,0x1e9390},
     {0x2d8e88,0x2d8e90},{0x1e46f0,0x1e4a60},
     0x1b14a0,0x1b35c0,0x1b4790,0x1b16d0,0x1fe130,0x791ac8,0x791acc,0x2e0f38,0x10001}
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
