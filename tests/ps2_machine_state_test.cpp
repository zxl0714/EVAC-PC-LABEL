#include "machine_state.h"
#include "state_slots.h"
#include "state_archive.h"
#include "gs_device.h"
#include "gs_device_sink.h"
#include "gs_command_queue.h"
#include "gs_completion_mailbox.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "Stubs/MemoryCard.h"
#include "Syscalls/Helpers/State.h"
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>

namespace {
using namespace cave::ps2;
using namespace std::chrono_literals;
constexpr uint32_t GuestEntry = 0x700000;
constexpr uint32_t EeData = 0x181000;
constexpr uint32_t IopData = 0x78000;
constexpr uint32_t GsData = 0x21000;
constexpr uint32_t Value = 0x84527619;
bool dispatched{};

void require(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}

void resumed(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    uint32_t iop{};
    require(ctx->pc == GuestEntry && getRegU32(ctx, 4) == Value, "Wrong saved EE PC or register");
    require(ram[EeData] == 93 && rt->vu0().state().vf[3][1] == 17.5f &&
            rt->vu1().state().vf[9][2] == -71.25f, "EE memory or VU state changed across process restart");
    require(rt->readIopMemory(IopData, &iop, sizeof(iop)) && iop == Value, "Wrong restored IOP memory");
    dispatched = true;
    rt->eeScheduler().requestStop();
}

void run(bool save, const std::filesystem::path& path, const MachineState::GraphicsOwner& graphics) {
    auto runtime = std::make_unique<PS2Runtime>();
    auto& r = *runtime;
    require(r.memory().initialize() && r.syncCoreSubsystems(), "Cannot initialize test machine");
    require(r.registerFunction(GuestEntry, resumed), "Cannot register guest code");
    auto* ram = r.memory().getRDRAM();
    auto paths = PS2Runtime::getIoPaths();
    paths.mcRoot = path / (save ? "old-mount" : "new-mount") / "mc0";
    PS2Runtime::setIoPaths(paths);
    R5900Context sdk{};
    auto card = [&](auto function, unsigned a = 0, unsigned b = 0, unsigned c = 0, unsigned d = 0) {
        SET_GPR_U32(&sdk, 4, a); SET_GPR_U32(&sdk, 5, b);
        SET_GPR_U32(&sdk, 6, c); SET_GPR_U32(&sdk, 7, d);
        function(ram, &sdk, &r);
        return int32_t(getRegU32(&sdk, 2));
    };
    auto sync = [&](int wanted) {
        require(card(ps2_stubs::sceMcSync, 1, 0x1000, 0x1004) == 1, "Card completion missing");
        int cmd{}, result{};
        std::memcpy(&cmd, ram + 0x1000, 4);
        std::memcpy(&result, ram + 0x1004, 4);
        require(cmd == wanted, "Wrong memory card command completion");
        return result;
    };
    R5900Context initial{};
    initial.pc = save ? GuestEntry : GuestEntry + 4;
    if (save) SET_GPR_U32(&initial, 4, Value);
    r.eeScheduler().reset(ram, initial);
    StateSlots slots(path, "machine-state-test");
    if (save) {
        g_rpc_clients[0x1300]={true,73,0x8000};
        g_rpc_servers[0x8000]={0x8000,0x1700};
        g_bootmode_addresses[3]=0x1f30010;
        g_sif_modules_by_id[7]={7,"cdrom0:TEST.IRX","cdrom0:test.irx",2,true};
        g_sif_module_id_by_path["cdrom0:test.irx"]=7;
        g_next_sif_module_id=8;
        card(ps2_stubs::sceMcInit);
        std::memcpy(ram + 0x1400, "/fixture", 9);
        card(ps2_stubs::sceMcOpen, 0, 0, 0x1400, PS2_FIO_O_RDWR | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC);
        require(sync(2) == 0, "Card fixture open failed");
        std::memcpy(ram + 0x1800, "abcdef", 6);
        card(ps2_stubs::sceMcWrite, 0, 0x1800, 6);
        require(sync(6) == 6, "Card fixture write failed");
        card(ps2_stubs::sceMcSeek, 0, 2, SEEK_SET);
        ram[EeData] = 93;
        r.vu0().state().vf[3][1] = 17.5f;
        r.vu1().state().vf[9][2] = -71.25f;
        require(r.writeIopMemory(IopData, &Value, sizeof(Value)), "Cannot set up IOP memory");
        graphics([](GsDevice& gs) {
            gs.WriteVram(GsData, {reinterpret_cast<const uint8_t*>(&Value), sizeof(Value)});
            auto mailbox = gs.Completions();
            mailbox->Finish(mailbox->Generation());
        });
        const auto archive = MachineState::Save(r, graphics);
        slots.Save(99, archive);
        slots.Remember(99);
        std::cout << "Saved " << archive.size() << " bytes before compression\n";
        return;
    }
    require(slots.LastUsed() == 99 && slots.List()[98], "Cold process lost the selected slot");
    auto bytes = slots.Read(99);
    // Reject a damaged final component after earlier components have already
    // been installed. The live RAM, IOP and graphics must all be rolled back.
    auto broken = bytes;
    StateReader walk(broken);
    uint32_t version{};
    walk.Field(version);
    std::span<const uint8_t> finalPart;
    while (true) {
        finalPart = walk.Blob();
        if (finalPart.data() + finalPart.size() == broken.data() + broken.size()) break;
    }
    broken[finalPart.data() - broken.data()] ^= 1;
    ram[EeData] = 37;
    bool rejected{};
    try { MachineState::Load(r, graphics, broken); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && ram[EeData] == 37, "Failed full-machine restore mutated live RAM");
    uint32_t iop{};
    require(r.readIopMemory(IopData, &iop, sizeof(iop)) && iop == 0, "IOP rollback failed");
    graphics([](GsDevice& gs) {
        uint32_t pixel{};
        gs.ReadbackVram(GsData, {reinterpret_cast<uint8_t*>(&pixel), sizeof(pixel)});
        require(pixel == 0 && !gs.Completions()->Take(), "GPU rollback failed");
    });
    MachineState::Load(r, graphics, bytes);
    require(g_rpc_clients.at(0x1300).busy && g_rpc_clients.at(0x1300).last_rpc==73 &&
        g_rpc_servers.at(0x8000).sd_ptr==0x1700 && g_bootmode_addresses.at(3)==0x1f30010 &&
        g_sif_module_id_by_path.at("cdrom0:test.irx")==7 && g_sif_modules_by_id.at(7).refCount==2 &&
        g_next_sif_module_id==8, "Cold SDK restore lost a pending RPC or loaded module");
    require(sync(4) == 2 && card(ps2_stubs::sceMcSync, 1, 0x1000, 0x1004) == -1,
            "Restored card seek must complete exactly once");
    card(ps2_stubs::sceMcRead, 0, 0x1a00, 4);
    require(sync(5) == 4 && std::memcmp(ram + 0x1a00, "cdef", 4) == 0,
            "Cold card restore changed file position or truncated existing contents");
    card(ps2_stubs::sceMcEnd);
    graphics([](GsDevice& gs) {
        uint32_t pixel{};
        gs.ReadbackVram(GsData, {reinterpret_cast<uint8_t*>(&pixel), sizeof(pixel)});
        require(pixel == Value && gs.Completions()->Take().finish, "Cold GS restore lost data or IRQ");
    });
    r.eeScheduler().run();
    require(dispatched, "Restored EE dispatch did not resume");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected save/load and a temporary profile path");
        GsDevice device;
        GsDeviceSink sink(device, [](const GsFrame&) {});
        GsCommandQueue queue(8u << 20, 2048, device.Completions());
        auto ee = std::async(std::launch::async, [&] {
            run(std::string_view(argv[1]) == "save", argv[2], [&](const auto& visit) {
                std::exception_ptr error;
                queue.RunOnConsumer([&] {
                    try { visit(device); } catch (...) { error = std::current_exception(); }
                });
                if (error) std::rethrow_exception(error);
            });
        });
        while (ee.wait_for(0ms) != std::future_status::ready) queue.ConsumeOne(sink, 1ms);
        ee.get();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
