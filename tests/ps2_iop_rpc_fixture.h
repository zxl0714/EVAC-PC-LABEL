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

// External EE RPCs execute IOP instructions too. Repeated calls must consume
// the same clock budget as scheduled workers, rather than creating extra PCM.
void IopRpcClockContract(bool benchmark=false) {
    auto host=DelayedThreadImage();
    std::fill(host.image.begin()+0x100,host.image.end(),uint8_t{});
    const uint32_t entry[]{
        0x27bdffe0,0xafbf001c,0x3c040001,0x34840400, // server data
        0x24051234,0x3c060001,0x34c60100,0x3c070001,0x34e70500,
        0xafa00010,0xafa00014,0xafa00018,0x0c004085,0, // RegisterRpc
        0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0
    };
    std::memcpy(host.image.data()+0x100,entry,sizeof(entry));
    const uint32_t body[]{0x24080100,0x2508ffff,0x1500fffe,0, // bounded CPU work
        0x03e00008,0x00a01021}; // reply = a1
    std::memcpy(host.image.data()+0x200,body,sizeof(body));
    const uint32_t imports[]{0x41e00000,0,0x101,0x63666973,0x0000646d,
        0x03e00008,0x24000011,0,0};
    std::memcpy(host.image.data()+0x300,imports,sizeof(imports));
    ps2x::iop::detail::IopEmulator emulator(host);
    Require(emulator.loadModuleBuffer(0,nullptr,0).startResult==0 && emulator.hasRpcServer(0x1234),
            "RPC clock fixture did not register its server");
    // Settle startup before measuring a full second at 48 kHz.
    emulator.runEeCycles(8u*491520u);
    std::array<int16_t,4096> pcm{};
    emulator.readPcm(pcm.data(),pcm.size()/2);
    const auto before=emulator.cycles();
    ps2x::iop::RpcRequest request{};request.sid=0x1234;
    size_t samples=0;
    for(unsigned i=0;i<750;++i) {
        Require(emulator.handleRpc(request).handled,"RPC clock fixture call failed");
        emulator.runEeCycles(8u*49152u);
        samples+=emulator.readPcm(pcm.data(),pcm.size()/2);
    }
    const auto elapsed=emulator.cycles()-before;
    std::cout<<"IOP external RPC clock: cycles="<<elapsed<<" expected=36864000 PCM="<<samples<<" expected=48000\n";
    if(!benchmark)Require(elapsed==36864000 && samples==48000,
                         "External RPC work created extra IOP time and PCM");
    emulator.reset();emulator.runEeCycles(8192);
    Require(emulator.cycles()==1024,"Reset retained external RPC clock budget");
}
