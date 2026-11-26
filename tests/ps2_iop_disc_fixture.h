#pragma once
#include <set>
#include "emulator/imports/iop_cdvd.h"
#include "emulator/imports/iop_ioman.h"
#include "emulator/services/iop_rpc.h"
#include "../third_party/ps2/runtime/iop_disc_host.h"
void IopIsoSearchContract() {
    using namespace ps2x::iop::detail;
    constexpr uint32_t Lba=0x12345,Size=4096,Name=0x18000,Result=0x18100,Data=0x19000;
    class Host : public AudioFixtureHost,public cave::ps2::IopDiscHost {
    public:
        std::string hostPath(ps2x::iop::HostPathKind kind) const override {
            return kind==ps2x::iop::HostPathKind::CdImage ? "disc.iso" : "";
        }
        bool FindDiscFile(std::string_view path,uint32_t& lba,uint32_t& size) const override {
            if(path!="cdrom0:\\ASSETS.PDM;1")return false;
            lba=Lba;size=Size;return true;
        }
        uint64_t openHostFile(std::string_view path) override {return path=="disc.iso"?3:0;}
        bool hostFileSize(uint64_t handle,uint64_t& size) const override {
            if(handle!=3)return false;
            size=uint64_t(Lba+2)*2048;
            return true;
        }
        bool readHostFile(uint64_t handle,uint64_t offset,void* output,size_t bytes,size_t& read) override {
            Require(handle==3 && (offset==uint64_t(Lba)*2048 || offset==uint64_t(Lba+1)*2048) && bytes==2048,
                    "IOP raw read did not use the LBA returned by ISO search");
            std::memset(output,offset==uint64_t(Lba)*2048?0x5a:0xb6,bytes);read=bytes;return true;
        }
    } host;
    IopMemory memory;IopKernel kernel(memory);IopCdvd disc(host,memory,kernel);
    Require(memory.allocate(0x3000,16,Name)==Name,"ISO fixture RAM allocation failed");
    const char path[]="cdrom0:\\ASSETS.PDM;1";
    Require(memory.writeRam(Name,path,sizeof(path)),"ISO fixture path write failed");
    IopCpuState call{};call.gpr[4]=Result;call.gpr[5]=Name;
    Require(disc.dispatchImport(10,call) && call.gpr[2]==1 && memory.read32(Result)==Lba &&
            memory.read32(Result+4)==Size,"IOP search returned a cache LBA instead of the original ISO extent");
    call.gpr[4]=Lba;call.gpr[5]=1;call.gpr[6]=Data;
    Require(disc.dispatchImport(6,call) && call.gpr[2]==1 && memory.read32(Data)==0x5a5a5a5a,
            "IOP search and sector read address different data");

    const auto streamCall = [&](uint16_t ordinal,uint32_t a0=0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0) {
        call.gpr[4]=a0;call.gpr[5]=a1;call.gpr[6]=a2;call.gpr[7]=a3;
        Require(disc.dispatchImport(ordinal,call),"IOP stream import was not recognized");
        return call.gpr[2];
    };
    constexpr uint32_t Error=Result+32;
    call.gpr[28] = 0x18700;
    Require(streamCall(37, 0x18500) == 0, "CD callback registration failed");
    Require(streamCall(59,Lba)==0 && streamCall(56,2,0,Data)==0 &&
            streamCall(56,2,2,Data)==1 && streamCall(59,Lba)==1 && streamCall(60)==2,
            "IOP stream initialization or initial sector count is incorrect");
    Require(streamCall(57,1,Data,0,Error)==1 && memory.read32(Data)==0x5a5a5a5a &&
            memory.read32(Error)==0 && streamCall(60)==1 &&
            streamCall(7,Lba)==1 && streamCall(67)==1 && streamCall(57,1,Data)==0,
            "IOP paused stream did not keep its own cursor");
    const auto state = disc.saveState();
    disc.reset();
    disc.loadState(state);
    auto completion = disc.takeCompletionCallback();
    Require(completion && completion->address == 0x18500 && completion->gp == 0x18700 &&
            completion->reason == 4 && !disc.takeCompletionCallback(),
            "IOP CD seek callback was lost or delivered twice after restore");
    auto damaged = state;
    damaged.pop_back();
    bool rejected = false;
    try { disc.loadState(damaged); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected && streamCall(57,1,Data)==0 && streamCall(68)==1 &&
            streamCall(57,2,Data,0,Error)==1 && memory.read32(Data)==0xb6b6b6b6 &&
            streamCall(60)==0,"IOP streaming did not preserve its own cursor or the disc boundary");
    Require(streamCall(58,Lba)==1 && streamCall(60)==2 && streamCall(61)==1 &&
            streamCall(57,1,Data)==0,"IOP stream seek or stop is incorrect");
    std::cout<<"IOP ISO metadata: raw and streaming reads agree with the original LBA: PASS\n";
}

void IopFileStateContract() {
    using namespace ps2x::iop::detail;
    class Host : public AudioFixtureHost {
    public:
        uint64_t nextHandle = 10, fileSize = 128;
        std::set<uint64_t> live;
        std::string mount = "first-disc";
        std::string translateGuestPath(std::string_view path) const override {
            return path == "cdrom0:/music.bin" ? mount : "";
        }
        uint64_t openHostFile(std::string_view path) override {
            if (path != mount) return 0;
            live.insert(++nextHandle);
            return nextHandle;
        }
        bool hostFileSize(uint64_t handle, uint64_t& size) const override {
            size = fileSize;
            return live.contains(handle);
        }
        bool readHostFile(uint64_t handle, uint64_t offset, void* data, size_t bytes, size_t& read) override {
            if (!live.contains(handle) || offset + bytes > fileSize) return false;
            for (size_t i = 0; i < bytes; ++i) static_cast<uint8_t*>(data)[i] = uint8_t(offset + i);
            read = bytes;
            return true;
        }
        void closeHostFile(uint64_t handle) override { live.erase(handle); }
    } host;
    class Executor : public IopGuestExecutor {
        uint32_t executeGuestFunction(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {
            throw std::logic_error("Unexpected guest callback");
        }
    } executor;
    IopMemory memory;
    IopIoman files(memory, host);
    constexpr uint32_t name = 0x18000, data = name + 128;
    constexpr char path[] = "cdrom0:/music.bin";
    Require(memory.allocate(256, 16, name) == name && memory.writeRam(name, path, sizeof(path)),
            "IOP file state fixture RAM setup failed");
    const auto call = [&](uint16_t ordinal, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0) {
        IopCpuState cpu{};
        cpu.gpr[4] = a0; cpu.gpr[5] = a1; cpu.gpr[6] = a2;
        Require(files.dispatchImport(ordinal, cpu, executor), "Missing IOP file import");
        return cpu.gpr[2];
    };
    const auto fd = call(4, name, 1);
    Require(fd == 3 && call(8, fd, 65) == 65, "IOP file seek failed");
    const auto state = files.saveState();
    files.reset();
    host.mount = "relocated-disc";
    files.loadState(state);
    Require(host.live.size() == 1 && host.nextHandle == 12 && call(6, fd, data, 4) == 4 &&
            memory.read32(data) == 0x44434241u, "Restored IOP descriptor did not resume at its saved offset");
    ++host.fileSize;
    bool rejected = false;
    try { files.loadState(state); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && host.live.size() == 1 && call(6, fd, data, 4) == 4 &&
            memory.read32(data) == 0x48474645u && call(4, name, 1) == 4,
            "Failed IOP restore leaked handles or changed the active file table");
    std::cout << "IOP open files: remount, cursor and rollback PASS\n";
}
