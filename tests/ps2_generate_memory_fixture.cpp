#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/Translators/instruction_translator.h"
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    using namespace ps2recomp;
    const std::vector<Symbol> symbols;
    const std::vector<Section> sections;
    CodeGenerator generator(symbols, sections);
    InstructionTranslator translator(generator);
    const std::array opcodes{OPCODE_LB, OPCODE_LH, OPCODE_LW, OPCODE_LBU, OPCODE_LHU,
        OPCODE_LWU, OPCODE_LD, OPCODE_LQ, OPCODE_SB, OPCODE_SH, OPCODE_SW, OPCODE_SD, OPCODE_SQ};
    // Expressions used by the pinned translator before instruction sharing.
    const std::array<std::string, 13> originals{
        "SET_GPR_S32(ctx, RT, (int8_t)READ8(ADDR));", "SET_GPR_S32(ctx, RT, (int16_t)READ16(ADDR));",
        "SET_GPR_S32(ctx, RT, (int32_t)READ32(ADDR));", "SET_GPR_ZE32(ctx, RT, (uint8_t)READ8(ADDR));",
        "SET_GPR_ZE32(ctx, RT, (uint16_t)READ16(ADDR));", "SET_GPR_ZE32(ctx, RT, READ32(ADDR));",
        "SET_GPR_U64(ctx, RT, READ64(ADDR));", "SET_GPR_VEC(ctx, RT, READ128(ADDR));",
        "WRITE8(ADDR, (uint8_t)GPR_U32(ctx, RT));", "WRITE16(ADDR, (uint16_t)GPR_U32(ctx, RT));",
        "WRITE32(ADDR, GPR_U32(ctx, RT));", "WRITE64(ADDR, GPR_U64(ctx, RT));", "WRITE128(ADDR, GPR_VEC(ctx, RT));"};
    auto replace = [](std::string text, const std::string& key, const std::string& value) {
        size_t at{};
        while ((at = text.find(key, at)) != std::string::npos) { text.replace(at, key.size(), value); at += value.size(); }
        return text;
    };
    auto reference = [&](unsigned op, unsigned rs, unsigned rt, int offset) {
        auto text = replace(originals[op], "RT", std::to_string(rt));
        return replace(text, "ADDR", "ADD32(GPR_U32(ctx, " + std::to_string(rs) + "), " + std::to_string(uint32_t(offset)) + "u)");
    };
    auto emitted = [&](unsigned op, unsigned rs, unsigned rt, int offset) {
        Instruction inst{}; inst.opcode = opcodes[op]; inst.rs = rs; inst.rt = rt;
        inst.simmediate = uint32_t(offset); inst.immediate = uint16_t(offset);
        auto text = translator.translate(inst, {});
        if (text.find("::cave::ps2::") == std::string::npos) throw std::runtime_error("Dynamic access was not shared");
        MemoryAccessHint hint{}; hint.hasAddress = true; hint.address = 0x100u;
        if (translator.translate(inst, hint).find("FAST_") == std::string::npos)
            throw std::runtime_error("Constant-address fast path changed");
        inst.isMmio = true;
        if (translator.translate(inst, {}).find("runtime->") == std::string::npos)
            throw std::runtime_error("Explicit MMIO path changed");
        return text;
    };
    std::ofstream out(argv[1]);
    out << "#pragma once\n#include \"ps2_runtime_macros.h\"\n"
        "using MemoryFixtureFunction = void (*)(uint8_t*, R5900Context*, PS2Runtime*);\n"
        "struct MemoryFixture { unsigned op, rs, rt; int offset; MemoryFixtureFunction original, shared; };\n";
    std::string entries;
    unsigned number{};
    for (unsigned op = 0; op < opcodes.size(); ++op)
        for (const auto [rs, rt] : std::array<std::pair<unsigned,unsigned>,6>{{{0,0},{0,2},{1,0},{1,1},{7,29},{31,2}}})
            for (int offset : {-32768, -1, 0, 1, 32767}) {
                const auto tag = std::to_string(number++);
                for (bool shared : {false,true})
                    out << "void " << (shared ? "Shared" : "Original") << tag
                        << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {\n"
                        << (shared ? emitted(op,rs,rt,offset) : reference(op,rs,rt,offset)) << "\n}\n";
                entries += "{" + std::to_string(op) + "," + std::to_string(rs) + "," + std::to_string(rt) + "," +
                    std::to_string(offset) + ",Original" + tag + ",Shared" + tag + "},\n";
            }
    out << "inline const MemoryFixture kMemoryFixtures[]{\n" << entries << "};\n";
    for (bool shared : {false,true}) {
        out << "__declspec(noinline) uint64_t " << (shared ? "Shared" : "Original")
            << "MemoryBench(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime, unsigned iterations) {\n"
               "uint64_t sum{};\nfor(unsigned n=0;n<iterations;++n) {\n";
        for (unsigned repeat=0;repeat<4;++repeat)
            for (unsigned op : {10u,2u,12u,7u,8u,3u})
                out << (shared ? emitted(op,7,29,int(repeat*32)) : reference(op,7,29,int(repeat*32))) << '\n';
        out << "sum+=GPR_U64(ctx,29); SET_GPR_U64(ctx,29,sum^n);\n}\nreturn sum;\n}\n";
    }
    return out ? 0 : 1;
}
