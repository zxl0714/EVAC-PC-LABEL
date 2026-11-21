#pragma once
#include "runtime/ps2_memory.h"
#include <array>
#include <cstring>
#include <vector>

namespace {
void VifDmaBurst() {
    PS2Memory memory;
    Require(memory.initialize(), "Could not initialize VIF DMA fixture");
    std::vector<std::vector<uint8_t>> packets;
    GifArbiter arbiter([&](GifPathId path, const uint8_t* data, uint32_t size) {
        Require(path == GifPathId::Path1, "Unexpected GIF path in VU DMA burst");
        packets.emplace_back(data, data + size);
    });
    memory.setGifArbiter(&arbiter);
    memory.setRawGifTransfers(true);
    uint8_t sequence = 0;
    const auto kick = [&] {
        std::array<uint8_t, 16> data{};
        data.fill(++sequence);
        arbiter.submitImmediate(GifPathId::Path1, data.data(), uint32_t(data.size()));
    };
    memory.setVu1MscalCallback([&](uint32_t, uint32_t, uint32_t) { kick(); });
    memory.setVu1MscntCallback([&](uint32_t, uint32_t) { kick(); });
    const std::array<uint32_t, 5> commands{
        0x14000000, 0x17000000, 0x11000000, 0x17000000, 0x15000000};
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(commands.data()), sizeof(commands));
    Require(packets.size() == 3 && packets[0].size() == 32 && packets[0][0] == 1 &&
            packets[0][16] == 2 && packets[1] == std::vector<uint8_t>(16, 3) &&
            packets[2] == std::vector<uint8_t>(16, 4), "XGKICK batch lost a VIF ordering boundary");
    unsigned submissions = 0;
    packets.clear();
    arbiter.setProcessBurstFn([&](auto ranges, auto bytes) {
        ++submissions;
        for (const auto& range : ranges) {
            Require(range.path == GifPathId::Path1, "Wrong VIF transport path");
            packets.emplace_back(bytes.begin(), bytes.begin() + range.size);
            bytes = bytes.subspan(range.size);
        }
    });
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(commands.data()), sizeof(commands));
    Require(submissions == 1 && packets.size() == 3 && packets[0].size() == 32 &&
            packets[0][0] == 5 && packets[0][16] == 6 && packets[1] == std::vector<uint8_t>(16, 7) &&
            packets[2] == std::vector<uint8_t>(16, 8), "Buffered VIF fences lost range or ordering");
}

void VifMixedBurst() {
    std::vector<unsigned> stream;
    unsigned batches = 0;
    const auto receive = [&](GifPathId path, const uint8_t* bytes, uint32_t size) {
        for (uint32_t pos = 0; pos < size; pos += 16)
            stream.push_back(256u * static_cast<unsigned>(path) + bytes[pos]);
    };
    GifArbiter arbiter(receive);
    arbiter.setProcessBurstFn([&](auto ranges, auto bytes) {
        ++batches;
        for (const auto& range : ranges) {
            receive(range.path, bytes.data(), range.size);
            bytes = bytes.subspan(range.size);
        }
        Require(bytes.empty(), "Mixed GIF burst lost a path range");
    });
    std::array<uint8_t, 16> packet{};
    auto send = [&](GifPathId path, uint8_t tag) {
        packet.fill(tag);
        arbiter.submitImmediate(path, packet.data(), uint32_t(packet.size()));
    };
    arbiter.runVifBurst([&] {
        send(GifPathId::Path2, 1);
        send(GifPathId::Path1, 2);
        Require(stream.empty(), "Mixed GIF burst unexpectedly drained");
        arbiter.flushVifBurst();
        Require(batches == 1 && stream == std::vector<unsigned>{513, 258}, "Mixed GIF fence reordered paths");
        for (unsigned i = 0; i < 1025; ++i)
            send((i & 1u) ? GifPathId::Path2 : GifPathId::Path1, uint8_t(i));
        Require(batches == 2 && stream.size() == 1026, "Mixed GIF size limit lost a transfer");
    });
    for (unsigned i = 0; i < 1025; ++i)
        Require(stream.at(i + 2) == ((i & 1u) ? 512u : 256u) + uint8_t(i), "Mixed GIF payload mismatch");
    try {
        arbiter.runVifBurst([&] { send(GifPathId::Path2, 3); throw 3; });
    } catch (int) {}
    arbiter.runVifBurst([&] { send(GifPathId::Path1, 4); });
    Require(stream.size() == 1028 && stream.back() == 260, "Abandoned GIF burst leaked into next submission");
}

void VifPath1Burst() {
    std::vector<std::pair<GifPathId, std::vector<uint8_t>>> transfers;
    GifArbiter arbiter([&](GifPathId path, const uint8_t* p, uint32_t size) {
        transfers.emplace_back(path, std::vector<uint8_t>(p, p + size));
    });
    std::array<uint8_t, 16> data{};
    auto send = [&](GifPathId path, uint8_t value) {
        data.fill(value);
        arbiter.submitImmediate(path, data.data(), uint32_t(data.size()));
    };
    arbiter.runVifBurst([&] {
        send(GifPathId::Path1, 1);
        send(GifPathId::Path1, 2);
        send(GifPathId::Path2, 3);
        send(GifPathId::Path1, 4);
    });
    Require(transfers.size() == 3 && transfers[0].first == GifPathId::Path1 &&
            transfers[0].second.size() == 32 && transfers[0].second[0] == 1 && transfers[0].second[16] == 2 &&
            transfers[1].first == GifPathId::Path2 && transfers[1].second[0] == 3 &&
            transfers[2].first == GifPathId::Path1 && transfers[2].second[0] == 4,
            "VU burst lost XGKICK ownership, order or its final packet");
    transfers.clear();
    arbiter.submit(GifPathId::Path3, data.data(), uint32_t(data.size()));
    send(GifPathId::Path1, 5);
    Require(transfers.size() == 2 && transfers[0].first == GifPathId::Path1 &&
            transfers[1].first == GifPathId::Path3, "Immediate path bypassed GIF arbitration");
}

void VifImageContinuation(PS2Memory& memory) {
    std::vector<std::vector<uint8_t>> received;
    GifArbiter arbiter([&](GifPathId path,const uint8_t* data,uint32_t bytes){
        Require(path==GifPathId::Path2,"VIF DIRECT lost GIF path2");
        received.emplace_back(data,data+bytes);
    });
    memory.setGifArbiter(&arbiter);
    const auto begin=[&] {
        std::array<uint8_t,20> command{};
        const uint32_t direct=0x50000003;
        const uint64_t tag=2u|(1u<<15)|(uint64_t(2)<<58);
        std::memcpy(command.data(),&direct,4);std::memcpy(command.data()+4,&tag,8);
        memory.processVIF1Data(command.data(),uint32_t(command.size()));
    };
    const std::array<uint8_t,32> image{0x42,0x24,0x66,0x99};
    memory.setRawGifTransfers(true);
    begin();memory.processVIF1Data(image.data(),uint32_t(image.size()));
    Require(received.size()==2 && received[0].size()==16 && received[1]==std::vector<uint8_t>(image.begin(),image.end()),
            "Raw VIF IMAGE continuation gained a synthetic GIF tag");
    received.clear();memory.setRawGifTransfers(false);
    begin();memory.processVIF1Data(image.data(),uint32_t(image.size()));
    Require(received.size()==2 && received[1].size()==48 && std::memcmp(received[1].data()+16,image.data(),32)==0,
            "CPU reference VIF IMAGE compatibility changed");
    received.clear();memory.setRawGifTransfers(true);
    // An IMAGE tag can end one DIRECT and receive pixels through a second
    // DIRECTHL. The second VIF code is not part of its pixel payload.
    std::array<uint8_t,20> header{};
    const uint32_t directHeader=0x50000001;const uint64_t tag=2u|(1u<<15)|(uint64_t(2)<<58);
    std::memcpy(header.data(),&directHeader,4);std::memcpy(header.data()+4,&tag,8);
    memory.processVIF1Data(header.data(),uint32_t(header.size()));
    std::array<uint8_t,36> pixels{};const uint32_t directPixels=0x51000002;
    std::memcpy(pixels.data(),&directPixels,4);std::memcpy(pixels.data()+4,image.data(),32);
    memory.processVIF1Data(pixels.data(),uint32_t(pixels.size()));
    Require(received.size()==2 && received[1]==std::vector<uint8_t>(image.begin(),image.end()),
            "VIF command leaked into a GIF IMAGE continuation across two DIRECT commands");
    received.clear();
    // CPU FIFO writes are 16 bytes: a DIRECT header can leave only 12
    // bytes of its next GIF qword in the first write. Preserve the tail.
    std::array<uint8_t,52> fifo{};const uint32_t fifoDirect=0x50000003;
    std::memcpy(fifo.data(),&fifoDirect,4);std::memcpy(fifo.data()+4,&tag,8);
    std::memcpy(fifo.data()+20,image.data(),32);
    for(size_t i=0;i<fifo.size();i+=16)
        memory.processVIF1Data(fifo.data()+i,uint32_t(std::min(size_t(16),fifo.size()-i)));
    std::vector<uint8_t> combined;
    for(const auto& packet:received)combined.insert(combined.end(),packet.begin(),packet.end());
    Require(combined==std::vector<uint8_t>(fifo.begin()+4,fifo.end()),
            "VIF DIRECT lost a partial GIF qword at a FIFO boundary");
    memory.setGifArbiter(nullptr);
    // DMA tag words, the following REF payload and a later payload may split
    // one MPG or UNPACK command. Do not discard or execute a partial upload.
    const uint32_t mpg=0x4a020003;
    const std::array<uint64_t,2> micro{0x123456789abcdef0ull,0xfedcba9876543210ull};
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&mpg),4);
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(micro.data()),8);
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(micro.data()+1),8);
    Require(!std::memcmp(memory.getVU1Code()+24,micro.data(),16),"Split VIF MPG upload was discarded");
    const auto generation = memory.getVU1CodeGeneration();
    auto upload = [&](const auto& code) {
        memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&mpg), 4);
        memory.processVIF1Data(reinterpret_cast<const uint8_t*>(code.data()), 16);
    };
    upload(micro);
    Require(memory.getVU1CodeGeneration() == generation, "An identical MPG invalidated compiled VU code");
    auto changed = micro;
    changed.back() ^= 1;
    upload(changed);
    Require(memory.getVU1CodeGeneration() != generation && !std::memcmp(memory.getVU1Code()+24,changed.data(),16),
            "Modified MPG left a stale compiled entry");
    const uint32_t unpack=0x6c020010;
    const std::array<uint32_t,8> vectors{1,2,3,4,5,6,7,8};
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&unpack),4);
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(vectors.data()),16);
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(vectors.data()+4),16);
    Require(!std::memcmp(memory.getVU1Data()+256,vectors.data(),32),"Split VIF UNPACK upload was discarded");
    const uint32_t wrappingUnpack = 0x6c0203ff;
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&wrappingUnpack), 4);
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(vectors.data()), 32);
    Require(!std::memcmp(memory.getVU1Data()+0x3ff0, vectors.data(), 16) &&
            !std::memcmp(memory.getVU1Data(), vectors.data()+4, 16), "V4-32 upload lost wrapped vectors");

    // Ibara reads an offscreen surface through VIF1, then samples the copy.
    // Check both DMA direction and an EE-visible completion before reuse.
    constexpr uint32_t receiver=0x20000,channel=0x10009000;
    memory.setGsFifoReader([&](uint8_t* dst,uint32_t count) {
        Require(count==image.size(),"Reverse VIF1 DMA requested the wrong byte count");
        std::memcpy(dst,image.data(),count);
        return count;
    });
    memory.writeIORegister(0x10003c00,1u<<23);
    memory.writeIORegister(channel+0x10,receiver);
    memory.writeIORegister(channel+0x20,2);
    memory.writeIORegister(channel,0x100);
    Require(!std::memcmp(memory.getRDRAM()+receiver,image.data(),image.size()) &&
            memory.readIORegister(channel+0x10)==receiver+image.size() &&
            memory.readIORegister(channel+0x20)==0 && !(memory.readIORegister(channel)&0x100) &&
            (memory.readIORegister(0x1000e010)&2),"GS pixels or completion lost in reverse VIF1 DMA");
    memory.writeIORegister(0x10003c00,0);
    memory.setGsFifoReader({});
}
}
