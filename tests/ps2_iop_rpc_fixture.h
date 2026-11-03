#pragma once
#include "emulator/services/iop_rpc.h"
#include "../third_party/ps2/runtime/iop_ee_rpc.h"

void IopRpcClientContract() {
    using namespace ps2x::iop::detail;
    class Host : public AudioFixtureHost, public cave::ps2::EeRpcHost {
    public:
        std::function<void(std::span<const uint8_t>)> completion;
        std::vector<uint8_t> sent;
        uint32_t FindEeRpc(uint32_t sid) const override { return sid == 0x1234 ? 0x200000 : 0; }
        bool QueueEeRpc(uint32_t server, uint32_t function, std::span<const uint8_t> request,
                        uint32_t size, std::function<void(std::span<const uint8_t>)> complete) override {
            Require(server == 0x200000 && function == 42 && size == 4, "Reverse RPC target or reply size corrupted");
            sent.assign(request.begin(), request.end()); completion = std::move(complete); return true;
        }
    } host;
    IopMemory memory;
    Require(memory.allocate(0x4000, 16, 0x18000) == 0x18000, "RPC fixture RAM reservation failed");
    IopKernel kernel(memory);
    class Executor : public IopGuestExecutor {
    public:
        IopMemory &memory;
        explicit Executor(IopMemory &value) : memory(value) {}
        uint32_t executeGuestFunction(uint32_t address, uint32_t a0, uint32_t a1,
                                     uint32_t a2, uint32_t, uint32_t) override {
            Require(address == 0x50000 && a0 == 42 && a2 == 4 && memory.read32(a1) == 0x12345678,
                    "Local RPC request reached wrong address or buffer");
            memory.write32(a1, 0x87654321); return a1;
        }
    } executor(memory);
    IopRpcBridge rpc(host, memory, kernel);
    constexpr uint32_t client = 0x18000, packet = 0x18100, reply = 0x18200, stack = 0x1a000;
    IopCpuState call{}; call.gpr[29] = stack;
    auto invoke = [&](unsigned ordinal) {
        Require(rpc.dispatchSifCmdImport(uint16_t(ordinal), call, executor), "IOP RPC import missing");
        Require(int32_t(call.gpr[2]) == 0 || ordinal == 17, "IOP RPC client failed");
    };
    call.gpr[4] = 0x18300; call.gpr[5] = 0x5555; call.gpr[6] = 0x50000; call.gpr[7] = 0x18400;
    invoke(17);
    call.gpr[4] = client; call.gpr[5] = 0x5555; invoke(15);
    memory.write32(packet, 0x12345678);
    memory.write32(stack + 16, 4); memory.write32(stack + 20, reply); memory.write32(stack + 24, 4);
    call.gpr[4] = client; call.gpr[5] = 42; call.gpr[6] = 0; call.gpr[7] = packet;
    invoke(16);
    Require(memory.read32(reply) == 0x87654321, "IOP local reply was not copied");
    call.gpr[4] = client; call.gpr[5] = 0x1234; invoke(15);
    Require(memory.read32(client + 0x24) == 0x200000, "IOP client did not bind EE server");
    memory.write32(packet, 0x12345678);
    call.gpr[4] = client; call.gpr[5] = 42; call.gpr[6] = 1; call.gpr[7] = packet;
    invoke(16);
    Require(memory.read32(client) == 1 && host.sent.size() == 4, "Async RPC lost busy state or payload");
    const std::array<uint8_t, 4> response{0xef, 0xbe, 0xad, 0xde};
    host.completion(response);
    Require(memory.read32(client) == 0 && memory.read32(reply) == 0xdeadbeef, "Async EE reply did not complete");
    memory.write32(packet, 0x12345678); invoke(16);
    rpc.reset(); memory.write32(reply, 0x1234); host.completion(response);
    Require(memory.read32(reply) == 0x1234, "Old RPC completion crossed an IOP reset");
    std::cout << "IOP RPC clients: local dispatch, EE binding, payload/reply, async state, reset: PASS\n";
}
