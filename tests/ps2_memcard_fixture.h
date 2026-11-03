#pragma once
#include "Stubs/MemoryCard.h"
#include <chrono>

namespace {
void MemoryCardHandles(PS2Runtime& runtime) {
  const auto root=std::filesystem::temp_directory_path()/
      ("evac-mc-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Require(std::filesystem::create_directory(root),"Memory-card fixture directory already exists");
  const auto previous=PS2Runtime::getIoPaths();auto paths=previous;paths.mcRoot=root/"mc0";
  PS2Runtime::setIoPaths(paths);
  auto* ram=runtime.memory().getRDRAM();R5900Context context{};
  const auto call=[&](PS2Runtime::RecompiledFunction function,uint32_t a=0,uint32_t b=0,uint32_t c=0,uint32_t d=0) {
    SET_GPR_U32(&context,4,a);SET_GPR_U32(&context,5,b);SET_GPR_U32(&context,6,c);SET_GPR_U32(&context,7,d);
    function(ram,&context,&runtime);return Result(context);
  };
  const auto sync=[&](int expectedCommand) {
    Require(call(ps2_stubs::sceMcSync,1,0x1100,0x1104)==1,"Memory-card operation did not complete");
    int32_t command,result;std::memcpy(&command,ram+0x1100,4);std::memcpy(&result,ram+0x1104,4);
    Require(command==expectedCommand,"Memory-card completion command changed");return result;
  };
  const auto open=[&](const char* name,uint32_t flags) {
    std::strcpy(reinterpret_cast<char*>(ram+0x1200),name);
    Require(call(ps2_stubs::sceMcOpen,0,0,0x1200,flags)==0,"Memory-card open was not queued");return sync(2);
  };
  const auto close=[&](unsigned fd) {
    Require(call(ps2_stubs::sceMcClose,fd)==0 && sync(3)==0,"Memory-card close failed");
  };
  call(ps2_stubs::sceMcInit);
  Require(open("/payload0",0x202)==0,"First libmc handle must be zero");
  for(unsigned i=0;i<256;++i)ram[0x2000+i]=uint8_t(i^0xa5);
  Require(call(ps2_stubs::sceMcWrite,0,0x2000,256)==0 && sync(6)==256,"Writing through handle zero failed");
  close(0);
  Require(open("/payload1",0x202)==0 && open("/payload2",0x202)==1,"libmc did not reuse the lowest free handle");
  close(0);
  Require(open("/payload1",2)==0,"libmc skipped a free zero handle while another file was open");
  close(0);close(1);
  Require(open("/payload0",1)==0,"Read-only libmc open did not reuse zero");
  Require(call(ps2_stubs::sceMcRead,0,0x3000,256)==0 && sync(5)==256,"Reading through handle zero failed");
  Require(std::memcmp(ram+0x2000,ram+0x3000,256)==0,"Memory-card round trip changed data");
  Require(call(ps2_stubs::sceMcRead,0,0x3000,1)==0 && sync(5)==0,"Memory-card EOF changed");
  close(0);
  Require(call(ps2_stubs::sceMcWrite,0,0x2000,1)==0 && sync(6)==-4,"Closed handle zero remained writable");
  const auto directory=[&](const char* pattern,uint32_t mode,int32_t count) {
    std::strcpy(reinterpret_cast<char*>(ram+0x1200),pattern);
    SET_GPR_U32(&context,8,uint32_t(count));SET_GPR_U32(&context,9,0x4000);
    Require(call(ps2_stubs::sceMcGetDir,0,0,0x1200,mode)==0,"Memory-card search was not queued");
    return sync(13);
  };
  std::memset(ram+0x4000,0xa5,192);
  Require(directory("/*",0,-1)==5,"Memory-card count query lost entries");
  Require(ram[0x4000]==0xa5 && ram[0x40bf]==0xa5,"Count-only search wrote a directory table");
  Require(directory("/*",0,2)==2,"Initial directory page changed");
  Require(std::strcmp(reinterpret_cast<char*>(ram+0x4020),".")==0 &&
          std::strcmp(reinterpret_cast<char*>(ram+0x4060),"..")==0,"Directory special entries changed");
  for(unsigned i=0;i<3;++i) {
    Require(directory("/*",1,1)==1,"Continuation did not return the next file");
    const auto name="payload"+std::to_string(i);
    Require(std::strcmp(reinterpret_cast<char*>(ram+0x4020),name.c_str())==0,"Continuation repeated an earlier file");
    uint32_t size;std::memcpy(&size,ram+0x4010,4);
    Require(size==(i==0?256u:0u),"Directory file size changed");
  }
  Require(directory("/*",1,1)==0 && directory("/*",1,1)==0,"Directory search did not stay at EOF");
  Require(directory("/payload?",0,1)==1 && directory("ignored",1,2)==2,"New search did not reset its cursor");
  Require(directory("/missing/*",0,1)==-4,"Missing directory lost its error result");
  call(ps2_stubs::sceMcEnd);PS2Runtime::setIoPaths(previous);
  for(const auto* name:{"payload0","payload1","payload2"})std::filesystem::remove(root/"mc0"/name);
  std::filesystem::remove(root/"mc0");std::filesystem::remove(root/"mc1");std::filesystem::remove(root);
}
}
