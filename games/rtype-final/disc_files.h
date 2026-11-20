#pragma once
#include <array>
#include "common/ps2_prepare.h"

namespace cave::rtype_final {
inline constexpr auto DiscSha256 = "3744ef0624bd342455e5de3116b23aa8a413d21c786b3c7da493cab20547cded";
inline constexpr std::array<Ps2PreparedFile, 4> DiscFiles{{
    {"SLUS_207.80", "boot.elf", 1665312, "9a058df5aeb11ee54d0e726731f7096cbf823538671c1afaa765111ee1106a1a"},
    {"SYSTEM.CNF", "SYSTEM.CNF", 57, "4bfa6b8e7140b5674ee0f6b52574ced139eec1f8928b83c8f59aab575d73e277"},
    {"IOP/IOP.DAT", "IOP/IOP.DAT", 327472, "4489700b4dad5e90046a741480223f9cbee122a79501e0797cdbc7995473224b"},
    {"IOP/IOPRP255.IMG", "IOP/IOPRP255.IMG", 264449, "e5762f40f94405d765b99b13b735a044197aab58b91b9f08d7832aa091eef40f"},
}};
}
