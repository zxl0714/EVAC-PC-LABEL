
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "cpu_reference.h"
#include <iostream>
#include <stdexcept>
#include <random>
using namespace ps2x::iop::detail;
bool equal(const IopCpuState&a,const IopCpuState&b) {
 return a.gpr==b.gpr && a.cop0==b.cop0 && a.hi==b.hi && a.lo==b.lo && a.pc==b.pc &&
 a.pendingLoadReg==b.pendingLoadReg && a.pendingLoadValue==b.pendingLoadValue &&
 a.pendingLoad==b.pendingLoad && a.branchPending==b.branchPending && a.branchTarget==b.branchTarget &&
 a.stopped==b.stopped && a.yielded==b.yielded && a.exception==b.exception;
}
int main() {
 IopMemory rm,cm;IopCpuReference ref(rm);IopCpuCore candidate(cm);std::mt19937 rng(0x708f40);
 unsigned nops=0,mixed=0;
 const uint32_t ops[]={0,0,0,0x00094880,0x012a4821,0x25290001,0x8d090000,0xad090000,
 0x11090004,0x08001000,0x03e00008,0x40096000,0x012a0018,0x012a001a,0x0000000c,0xffffffff};
 for(unsigned i=0;i<200000;++i) {
  IopCpuState a{};
  for(auto& v:a.gpr)v=rng();for(auto&v:a.cop0)v=rng();
  a.hi=rng();a.lo=rng();a.pc=rng();a.pendingLoadReg=rng()&31;a.pendingLoadValue=rng();
  a.pendingLoad=rng()&1;a.branchPending=rng()&1;a.branchTarget=rng();a.stopped=rng()&1;
  a.yielded=rng()&1;a.exception=rng()&1;
  a.gpr[8]=0x18000+(rng()&0xfffc);a.gpr[10]=rng()|1u;
  auto b=a;uint32_t instruction=i<100000?0:ops[rng()%std::size(ops)];
  const bool ar=ref.executeInstruction(a,instruction),br=candidate.executeInstruction(b,instruction);
  if(ar!=br || !equal(a,b) || rm.read32(b.gpr[8])!=cm.read32(b.gpr[8]))
    throw std::runtime_error("CPU-state divergence at "+std::to_string(i));
  instruction==0?++nops:++mixed;
 }
 std::cout<<"Complete CPU-state differential PASS cases=200000 nop="<<nops<<" mixed="<<mixed<<'\n';
}
