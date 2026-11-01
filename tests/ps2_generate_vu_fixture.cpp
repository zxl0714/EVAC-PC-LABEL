#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2recomp/Translators/fpu_translator.h"
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
  if(argc!=2)return 1;
  const std::vector<ps2recomp::Symbol> symbols;
  const std::vector<ps2recomp::Section> sections;
  ps2recomp::CodeGenerator generator(symbols,sections);
  ps2recomp::Instruction instruction{};instruction.rd=27;
  std::ofstream output(argv[1]);
  output<<"#pragma once\n#include <cstdint>\n"
          "template<class Context,class Runtime>\n"
          "void GeneratedVcallmsr(uint8_t* rdram,Context* ctx,Runtime* runtime)\n"
        <<generator.translateVU_VCALLMSR(instruction)<<'\n';
  constexpr uint32_t sqrtWord=0x46160504; // SQRT.S f20, f22; fs bits encode zero.
  ps2recomp::Instruction squareRoot{};
  squareRoot.rs=(sqrtWord>>21)&31;squareRoot.rt=(sqrtWord>>16)&31;
  squareRoot.rd=(sqrtWord>>11)&31;squareRoot.sa=(sqrtWord>>6)&31;
  squareRoot.function=sqrtWord&63;
  ps2recomp::FpuTranslator floatingPoint(generator);
  output<<"template<class Context> void GeneratedSquareRoot(Context* ctx){\n"
        <<floatingPoint.translate(squareRoot)<<"\n}\n";
  return output?0:1;
}
