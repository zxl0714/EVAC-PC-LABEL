#include "daioujou_death_label.h"
#include "loader/image_memory.h"
#include "native_video.h"
#include "common/settings.h"
#include "common/verification_cache.h"
#include "games/daioujou-ps2/disc_files.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

#define KT_SYSV __attribute__((sysv_abi))

namespace cave::ps4 {
namespace {
using Vm = void*;
using Closure = int(KT_SYSV*)(Vm);
struct SquirrelApi {
    int(KT_SYSV* registerStrings)(Vm){};
    void(KT_SYSV* pushString)(Vm, const char*, int){};
    void(KT_SYSV* newClosure)(Vm, Closure, unsigned){};
    int(KT_SYSV* newSlot)(Vm, int, unsigned){};
    int(KT_SYSV* getTop)(Vm){};
    void(KT_SYSV* pop)(Vm,int){};
    int(KT_SYSV* getInteger)(Vm,int,int*){};
};
struct SquirrelLayout {
    std::string_view contract;
    std::size_t registration, strings, pushString, newClosure, newSlot, getTop, pop, getInteger;
};
constexpr std::array layouts{
    SquirrelLayout{"8256abd9bbe18ff68c5196d0104b84b94254fb355fcb0ca3df2c20b03802df79",
        0xb2960, 0xbddb0, 0x5ccd0, 0x5de20, 0x5f7e0, 0x5d4d0, 0x5f740, 0x5ee90},
    SquirrelLayout{"365b5db9be3808ecfffe8e9ddd28ac1b740a0326cd0d16fb0c63af22a76a3c2f",
        0xb2bf0, 0xbe040, 0x5cf60, 0x5e0b0, 0x5fa70, 0x5d760, 0x5f9d0, 0x5f120},
};
SquirrelApi squirrel;
DaioujouDeathLabel* active{};

bool Matches(std::span<const std::uint8_t,32> digest, std::string_view hex) {
    constexpr std::string_view alphabet = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (hex[2*i] != alphabet[digest[i] >> 4] || hex[2*i+1] != alphabet[digest[i] & 15]) return false;
    }
    return true;
}

int KT_SYSV RunFromMenu(Vm vm) {
    auto result = DeathLabelResult::Failed;
    try {
        DeathLabelActions actions;
        const std::array masks{&actions.shot, &actions.bomb, &actions.rapid};
        if (squirrel.getTop(vm) != int(masks.size()) + 1)
            throw std::runtime_error("Invalid Death Label action mapping");
        for (std::size_t i = 0; i < masks.size(); ++i) {
            int mask{};
            if (squirrel.getInteger(vm, int(i) + 2, &mask) < 0 || mask < 0 || mask > 0xffff)
                throw std::runtime_error("Invalid Death Label pad mask");
            *masks[i] = static_cast<std::uint16_t>(mask);
        }
        std::printf("{\"event\":\"daioujou_death_label_controls\",\"shot\":%u,\"bomb\":%u,\"rapid\":%u}\n",
            unsigned(actions.shot), unsigned(actions.bomb), unsigned(actions.rapid));
        result = active->Run(actions);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Death Label presentation: %s\n", error.what());
    }
    std::printf("{\"event\":\"daioujou_death_label_return\",\"result\":%d}\n", static_cast<int>(result));
    return 0;
}

int KT_SYSV RegisterDeathLabel(Vm vm) {
    const int result = squirrel.registerStrings(vm);
    // The caller opened the VM and pushed its root table before registering
    // the standard libraries. Keep that table on the stack for the next one.
    const int top = squirrel.getTop(vm);
    squirrel.pushString(vm, "evac_run_death_label", -1);
    squirrel.newClosure(vm, RunFromMenu, 0);
    const bool registered = squirrel.newSlot(vm, -3, 0) >= 0;
    const int extra = squirrel.getTop(vm) - top;
    if (extra > 0) squirrel.pop(vm, extra);
    std::printf("{\"event\":\"daioujou_death_label_script_binding\",\"registered\":%s}\n",
        registered ? "true" : "false");
    return result;
}
}

DaioujouDeathLabel::~DaioujouDeathLabel() {
    if (active == this) {
        active = nullptr;
        squirrel = {};
    }
    if (thunk_) VirtualFree(thunk_, 0, MEM_RELEASE);
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
}

bool DaioujouDeathLabel::Prepare(const fs::path& root, const Ini& ini, std::string_view profile) {
    const auto& game = FindGame("daioujou-ps2");
    try {
        const auto spec = PrepareLaunch(game, root, ini, profile);
        VerificationCache verification(root);
        const auto disc = Inspect(game, root, spec.paths.data, CheckLevel::Programs, &verification);
        if (disc.disc_image.empty()) throw std::runtime_error("PS2 DaiOuJou ISO disappeared during startup");
        disc_ = disc.disc_image;
        prepared_ = PreparePs2Disc(spec.paths, disc_, daioujou_ps2::DiscSha256, daioujou_ps2::DiscFiles);
        profile_ = spec.paths.profile;
        volume_ = static_cast<float>(ResolveSettings(ini, "daioujou").volume);

        const auto dll = DataChild(root, "runtime/ps2/daioujou-ps2-runtime.dll");
        library_ = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library_) throw std::runtime_error("Cannot load the PS2 DaiOuJou runtime");
        entry_ = reinterpret_cast<RunDeathLabel>(GetProcAddress(static_cast<HMODULE>(library_), DeathLabelEntry));
        if (!entry_) throw std::runtime_error("PS2 DaiOuJou runtime has no Death Label entry");
        active = this;
        std::puts("{\"event\":\"daioujou_death_label_data_ready\"}");
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Death Label unavailable: %s\n", error.what());
        return false;
    }
}

DeathLabelResult DaioujouDeathLabel::Run(const DeathLabelActions& actions) const {
    DeathLabelHost host;
    host.prepared = prepared_.c_str();
    host.disc = disc_.c_str();
    host.profile = profile_.c_str();
    host.volume = volume_;
    host.actions = &actions;
    return RunNativeVideoDeathLabel(host, entry_);
}

void DaioujouDeathLabel::InstallBinding(std::span<std::uint8_t> image, std::span<const std::uint8_t,32> identity) {
    if (!active || active->thunk_) return;
    constexpr std::array<std::uint8_t,5> call{0xe8, 0x4b, 0xb4, 0, 0};
    const SquirrelLayout* layout{};
    for (const auto& candidate : layouts) {
        if (Matches(identity, candidate.contract)) {
            layout = &candidate;
            break;
        }
    }
    if (!layout || image.size() < layout->strings + 1 ||
        std::memcmp(image.data() + layout->registration, call.data(), call.size()) != 0) {
        std::puts("{\"event\":\"daioujou_death_label_unavailable\",\"reason\":\"unrecognized_script_vm\"}");
        return;
    }

    // Both the game image and this trampoline are allocated below 2 GiB,
    // so the replacement CALL is in range regardless of host DLL placement.
    const auto reservation = ReserveImageMemory(kImageAllocationAlignment);
    if (!reservation.address) return;
    const auto release = [](void* p) { VirtualFree(p, 0, MEM_RELEASE); };
    std::unique_ptr<void, decltype(release)> thunk(reservation.address, release);
    if (!VirtualAlloc(thunk.get(), 4096, MEM_COMMIT, PAGE_READWRITE)) return;
    std::array<std::uint8_t,12> jump{0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    const auto target = reinterpret_cast<std::uintptr_t>(&RegisterDeathLabel);
    std::memcpy(jump.data() + 2, &target, sizeof(target));
    std::memcpy(thunk.get(), jump.data(), jump.size());
    DWORD previous{};
    if (!VirtualProtect(thunk.get(), 4096, PAGE_EXECUTE_READ, &previous)) return;

    auto* code = image.data() + layout->registration;
    const auto next = reinterpret_cast<std::intptr_t>(code + call.size());
    const auto offset = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(thunk.get()) - next);
    squirrel = {
        reinterpret_cast<decltype(squirrel.registerStrings)>(image.data() + layout->strings),
        reinterpret_cast<decltype(squirrel.pushString)>(image.data() + layout->pushString),
        reinterpret_cast<decltype(squirrel.newClosure)>(image.data() + layout->newClosure),
        reinterpret_cast<decltype(squirrel.newSlot)>(image.data() + layout->newSlot),
        reinterpret_cast<decltype(squirrel.getTop)>(image.data() + layout->getTop),
        reinterpret_cast<decltype(squirrel.pop)>(image.data() + layout->pop),
        reinterpret_cast<decltype(squirrel.getInteger)>(image.data() + layout->getInteger),
    };
    std::memcpy(code + 1, &offset, sizeof(offset));
    active->thunk_ = thunk.release();
}
}
