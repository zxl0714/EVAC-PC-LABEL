#include "state_slots.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid state unexpectedly succeeded");
}
}
int main() {
    try {
        const auto root = fs::temp_directory_path() /
            ("evac-state-slots-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(root)) throw std::runtime_error("Could not create state fixture");
        struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
        const auto directory = root / "profile1" / "states";
        cave::ps2::StateSlots slots(directory, "game/disc/standalone");
        const auto empty = slots.List();
        Require(slots.LastUsed() == 1 && std::ranges::none_of(empty, [](auto& x){ return x.has_value(); }) &&
                !fs::exists(directory), "Empty slots allocated storage or selected the wrong slot");
        std::vector<uint8_t> bytes(4u << 20);
        std::mt19937 random(1234);
        for (size_t i = 0; i < bytes.size() / 2; ++i) bytes[i] = uint8_t(random());
        slots.Save(99, bytes);
        slots.Remember(99);
        cave::ps2::StateSlots reopened(directory, "game/disc/standalone");
        auto listed = reopened.List();
        Require(reopened.LastUsed() == 99 && listed.back() && !listed.front() &&
                std::ranges::count_if(listed, [](auto& x){ return x.has_value(); }) == 1 &&
                reopened.Read(99) == bytes && fs::file_size(directory / "slot-99.state") < bytes.size(),
                "Compressed slot or remembered selection failed after reopening");
        cave::ps2::StateSlots otherProfile(root / "profile2", "game/disc/standalone");
        Require(otherProfile.LastUsed() == 1 && !otherProfile.List().back(), "State leaked into another profile");
        cave::ps2::StateSlots wrongProgram(directory, "game/disc/hosted");
        Reject([&]{ wrongProgram.Read(99); });
        constexpr std::array<std::string_view,2> programs{"standalone","hosted"};
        const auto sibling=wrongProgram.ReadProgram(99,programs);
        Require(sibling.name=="standalone" && sibling.bytes==bytes,"Same-disc program was not restored");
        cave::ps2::StateSlots differentDisc(directory,"game/another-disc/hosted");
        Reject([&]{ differentDisc.ReadProgram(99,programs); });
        Reject([&]{ reopened.Save(99, {}); });
        Reject([&]{ reopened.Save(100, bytes); });
        Reject([&]{ reopened.Remember(0); });
        Require(reopened.Read(99) == bytes && reopened.LastUsed() == 99, "Rejected save damaged existing slot");
        bytes.assign(1024, 0x42);
        reopened.Save(99, bytes);
        reopened.Save(1, bytes);
        reopened.Remember(1);
        Require(reopened.Read(99) == bytes && slots.LastUsed() == 1 && slots.List().front(),
                "Slot overwrite or switching selection failed");
        const auto path = directory / "slot-99.state";
        std::ofstream(path, std::ios::binary | std::ios::app).put('x');
        Reject([&]{ reopened.Read(99); });
        fs::resize_file(path, fs::file_size(path) - 3);
        Reject([&]{ reopened.Read(99); });
        Require(reopened.Read(1) == bytes, "Damaged slot affected its neighbor");
        std::cout << "PS2 slots: on-demand 01-99, compression, overwrite, profile isolation and integrity PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
