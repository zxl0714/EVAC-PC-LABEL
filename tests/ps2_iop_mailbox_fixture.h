#pragma once

// Authored packets and threads; no game or firmware data.
void IopMailboxContract() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    IopKernel kernel(memory);
    IopCpuState call{};
    auto mbx = [&](unsigned ordinal, uint32_t a0, uint32_t a1 = 0) {
        call.gpr[4] = a0; call.gpr[5] = a1;
        Require(kernel.dispatchMailboxImport(uint16_t(ordinal), call), "Mailbox ABI ordinal missing");
        return int32_t(call.gpr[2]);
    };
    constexpr uint32_t descriptor = 0x18000, output = 0x18100, packet = 0x18200;
    memory.write32(descriptor, 4); memory.write32(descriptor + 4, 0x1234);
    const int priorityBox = mbx(4, descriptor);
    Require(priorityBox > 0, "Mailbox allocation failed");
    for (unsigned i = 0; i < 3; ++i) {
        memory.write8(packet + i * 16 + 4, i == 0 ? 8 : 2);
        Require(mbx(6, priorityBox, packet + i * 16) == 0, "Mailbox send failed");
    }
    Require(mbx(11, priorityBox, output) == 0 && memory.read32(output) == 4 &&
        memory.read32(output + 4) == 0x1234 && memory.read32(output + 12) == 3 &&
        memory.read32(output + 16) == packet + 16, "Mailbox status lost packet count or priority head");
    for (const uint32_t expected : {packet + 16, packet + 32, packet})
        Require(mbx(9, output, priorityBox) == 0 && memory.read32(output) == expected,
                "Message priority or stable tie ordering failed");
    memory.write32(output, 0xfeed);
    Require(mbx(9, output, priorityBox) == -424 && memory.read32(output) == 0xfeed,
            "Empty poll changed output or failed to report no message");
    memory.write32(descriptor, 0);
    const int fifoBox = mbx(4, descriptor);
    Require(mbx(7, fifoBox, packet) == 0 && mbx(6, fifoBox, packet + 16) == 0,
            "FIFO send failed");
    Require(mbx(8, output, fifoBox) == 0 && memory.read32(output) == packet,
            "FIFO ignored arrival order");
    auto thread = [&](uint32_t priority) {
        memory.write32(descriptor, 0); memory.write32(descriptor + 4, 0);
        memory.write32(descriptor + 8, 0x50000); memory.write32(descriptor + 12, 0x400);
        memory.write32(descriptor + 16, priority); call.gpr[4] = descriptor;
        Require(kernel.dispatchThreadImport(4, call, 0), "Mailbox thread setup failed");
        const int id = int(call.gpr[2]); call.gpr[4] = id; call.gpr[5] = 0;
        Require(kernel.dispatchThreadImport(6, call, 0), "Mailbox thread start failed");
        return id;
    };
    const int high = thread(10), low = thread(40);
    auto *running = kernel.beginNextReady(0);
    Require(running && running->id == high, "Mailbox test priority failed");
    running->cpu.gpr[4] = output; running->cpu.gpr[5] = priorityBox;
    Require(kernel.dispatchMailboxImport(8, running->cpu) && running->cpu.yielded &&
        running->state == IopThreadState::Mailbox, "ReceiveMbx did not block empty receiver");
    kernel.endTimeslice(*running, 0x1fffff00);
    running = kernel.beginNextReady(1);
    Require(running && running->id == low, "Blocked mailbox thread remained schedulable");
    kernel.endTimeslice(*running, 0x1fffff00);
    Require(mbx(7, priorityBox, packet) == 0 && memory.read32(output) == packet,
            "Interrupt send failed direct delivery");
    running = kernel.beginNextReady(2);
    Require(running && running->id == high && running->cpu.gpr[2] == 0,
            "Mailbox wake did not invalidate ready election");
    running->cpu.gpr[4] = output; running->cpu.gpr[5] = priorityBox;
    Require(kernel.dispatchMailboxImport(8, running->cpu), "Second mailbox receive failed");
    kernel.endTimeslice(*running, 0x1fffff00);
    Require(mbx(5, priorityBox) == 0, "DeleteMbx failed");
    running = kernel.beginNextReady(3);
    Require(running && running->id == high && int32_t(running->cpu.gpr[2]) == -425,
            "Mailbox deletion failed to release waiter with delete error");
    kernel.endTimeslice(*running, 0x1fffff00);
    Require(mbx(9, output, priorityBox) == -410, "Deleted mailbox remained usable");
    kernel.reset();
    Require(mbx(11, fifoBox, output) == -410, "Reset retained a mailbox");
    std::cout << "IOP mailbox: ABI, FIFO, priority, poll, wait, wake, delete, reset: PASS\n";
}

void IopFixedPoolContract() {
    using namespace ps2x::iop::detail;
    IopMemory memory;
    IopKernel kernel(memory);
    IopCpuState call{};
    auto pool = [&](unsigned ordinal, uint32_t a0, uint32_t a1 = 0) {
        call.gpr[4] = a0; call.gpr[5] = a1;
        Require(kernel.dispatchFixedPoolImport(uint16_t(ordinal), call), "Fixed pool ordinal missing");
        return int32_t(call.gpr[2]);
    };
    const auto freeBefore = memory.maxFreeMemory();
    constexpr uint32_t descriptor = 0x18000, status = 0x18100;
    memory.write32(descriptor, 0); memory.write32(descriptor + 4, 0x1234);
    memory.write32(descriptor + 8, 19); memory.write32(descriptor + 12, 2);
    const int id = pool(4, descriptor);
    const auto a = pool(7, id), b = pool(8, id);
    Require(id > 0 && a > 0 && b > a && (a & 3) == 0 && b - a == 20,
            "Fixed pool failed aligned contiguous allocation");
    Require(pool(7, id) == -426, "Exhausted nonblocking pool did not fail");
    Require(pool(9, id, a + 1) < 0, "Misaligned fixed block was freed");
    Require(pool(9, id, a) == 0 && pool(9, id, a) < 0 && pool(7, id) == a,
            "Fixed block recycle or double-free validation failed");
    Require(pool(11, id, status) == 0 && memory.read32(status + 8) == 19 &&
        memory.read32(status + 12) == 2 && memory.read32(status + 16) == 0,
        "Fixed pool status failed");
    memory.write32(descriptor + 8, 0x50000); memory.write32(descriptor + 12, 0x400);
    memory.write32(descriptor + 16, 10); call.gpr[4] = descriptor;
    Require(kernel.dispatchThreadImport(4, call, 0), "Pool thread creation failed");
    call.gpr[4] = call.gpr[2]; call.gpr[5] = 0;
    Require(kernel.dispatchThreadImport(6, call, 0), "Pool thread start failed");
    auto* running = kernel.beginNextReady(0);
    Require(running != nullptr, "Pool receiver thread missing");
    running->cpu.gpr[4] = id;
    Require(kernel.dispatchFixedPoolImport(6, running->cpu) && running->state == IopThreadState::FixedPool,
            "Exhausted blocking allocation did not wait");
    kernel.endTimeslice(*running, 0x1fffff00);
    Require(kernel.beginNextReady(1) == nullptr, "Blocked allocator remained runnable");
    Require(pool(9, id, b) == 0, "Free did not wake allocator");
    running = kernel.beginNextReady(2);
    Require(running && int32_t(running->cpu.gpr[2]) == b, "Waiting allocator did not receive released block");
    kernel.endTimeslice(*running, 0x1fffff00);
    Require(pool(5, id) == 0 && pool(7, id) == -411, "Pool deletion failed");
    kernel.reset();
    // Thread stacks belong to the memory reset, rather than to pool deletion.
    memory.reset();
    Require(memory.maxFreeMemory() == freeBefore, "Pool lifecycle leaked IOP memory");
    std::cout << "IOP fixed pool: alignment, exhaustion, recycle, wait, wake, delete, reset: PASS\n";
}

void IopThreadArgumentsContract() {
    using namespace ps2x::iop::detail;
    IopMemory memory; IopKernel kernel(memory); IopCpuState call{};
    constexpr uint32_t descriptor=0x18000, source=0x18100;
    memory.write32(descriptor,0);memory.write32(descriptor+4,0);
    memory.write32(descriptor+8,0x50000);memory.write32(descriptor+12,0x400);
    memory.write32(descriptor+16,10);call.gpr[4]=descriptor;
    Require(kernel.dispatchThreadImport(4,call,0),"Argument fixture thread creation failed");
    const auto id=call.gpr[2];memory.write32(source,0x12345678);
    call.gpr[4]=id;call.gpr[5]=4;call.gpr[6]=source;
    Require(kernel.dispatchThreadImport(7,call,0) && call.gpr[2]==0,"StartThreadArgs failed");
    memory.write32(source,0xdeadbeef);
    const auto* thread=kernel.beginNextReady(0);
    Require(thread && thread->cpu.gpr[4]==4 && thread->cpu.gpr[5]!=source &&
        memory.read32(thread->cpu.gpr[5])==0x12345678 && thread->cpu.gpr[29]<thread->cpu.gpr[5],
        "IOP thread retained a pointer to its caller's overwritten argument stack");
    std::cout<<"IOP StartThreadArgs: owned aligned stack copy and caller lifetime: PASS\n";
}
