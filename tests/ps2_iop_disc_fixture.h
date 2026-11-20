#pragma once
#include "emulator/imports/iop_cdvd.h"
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
    Require(streamCall(59,Lba)==0 && streamCall(56,2,0,Data)==0 &&
            streamCall(56,2,2,Data)==1 && streamCall(59,Lba)==1 && streamCall(60)==2,
            "IOP stream initialization or initial sector count is incorrect");
    Require(streamCall(57,1,Data,0,Error)==1 && memory.read32(Data)==0x5a5a5a5a &&
            memory.read32(Error)==0 && streamCall(60)==1 &&
            streamCall(7,Lba)==1 && streamCall(67)==1 && streamCall(57,1,Data)==0 &&
            streamCall(68)==1 && streamCall(57,2,Data,0,Error)==1 && memory.read32(Data)==0xb6b6b6b6 &&
            streamCall(60)==0,"IOP streaming did not preserve its own cursor or the disc boundary");
    Require(streamCall(58,Lba)==1 && streamCall(60)==2 && streamCall(61)==1 &&
            streamCall(57,1,Data)==0,"IOP stream seek or stop is incorrect");
    std::cout<<"IOP ISO metadata: raw and streaming reads agree with the original LBA: PASS\n";
}
