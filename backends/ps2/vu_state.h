#pragma once
#include <cstdint>
#include <span>
#include <vector>

class VU1Interpreter;

namespace cave::ps2 {
struct VuStateArchive {
    static std::vector<uint8_t> Save(const VU1Interpreter& vu);
    static void Load(VU1Interpreter& vu, std::span<const uint8_t> state);
private:
    template<class Archive, class Unit> static void Transfer(Archive& archive, Unit& vu);
};
}
