#include "cpu/compatibility.h"
#include "cpu/assembler.h"
#include <immintrin.h>
#include <array>
#include <barrier>
#include <bit>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace cave::ps4::cpu;
namespace {
struct Fixture {
    std::uint8_t *bytes =
        static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Fixture() {
        if (!bytes)
            throw std::runtime_error("Vector fixture allocation");
    }
    ~Fixture() { VirtualFree(bytes, 0, MEM_RELEASE); }
    void Protect(DWORD protection) {
        DWORD old{};
        if (!VirtualProtect(bytes, 4096, protection, &old))
            throw std::runtime_error("Vector fixture protection");
        FlushInstructionCache(GetCurrentProcess(), bytes, 4096);
    }
};
struct alignas(32) State {
    std::uint32_t vectors[4][8], memory[8];
    std::uint64_t general[4], flags;
    unsigned mxcsr;
};
State Seed(unsigned pattern) {
    State state{};
    constexpr std::uint32_t fp[] = {0,          0x80000000, 0x3fc00000, 0xbf400000,
                                    0x7f800000, 0x7fc00005, 0x7f800003, 1};
    for (unsigned i = 0; i < 40; ++i) {
        const std::uint32_t bits =
            pattern < 2 ? fp[(i + (i / 8) * 3 + pattern) % 8] : 0x3d400001u + i * 0x01933713u;
        if (i < 32)
            state.vectors[i / 8][i % 8] = bits;
        else
            state.memory[i - 32] = bits;
    }
    for (unsigned i = 0; i < 4; ++i)
        state.general[i] = 0xb137e445a0092c85ull + i;
    return state;
}
void MoveVectors(Assembler &code, bool store) {
    for (unsigned i = 0; i < 4; ++i) {
        const auto v = Reg(static_cast<ZydisRegister>(ZYDIS_REGISTER_YMM0 + i)),
                   mem = Mem(ZYDIS_REGISTER_RCX, 32 * i, 32);
        code.Emit(ZYDIS_MNEMONIC_VMOVDQU, {store ? mem : v, store ? v : mem}, ZYDIS_ENCODABLE_ENCODING_VEX);
    }
}
using Body = void (*)(State *);
void Invoke(Body body, State &data, unsigned pattern) {
    const unsigned saved = _mm_getcsr();
    _mm_setcsr(0x1f80u | ((pattern & 3) << 13) | (pattern == 1 ? 0x8040u : 0));
    body(&data);
    _mm_setcsr(saved);
}
void CheckCase(const Image *&trap, std::span<const std::uint8_t> instruction, const std::string &name) {
    Fixture fixture;
    Assembler prefix(fixture.bytes, 4096);
    MoveVectors(prefix, false);
    for (unsigned i = 0; i < 4; ++i)
        prefix.Emit(ZYDIS_MNEMONIC_MOV,
                    {Reg(Gpr(8 + i)), Mem(ZYDIS_REGISTER_RCX, offsetof(State, general) + 8 * i, 8)});
    prefix.Emit(ZYDIS_MNEMONIC_CMP, {Reg(Gpr(8)), Reg(Gpr(8))});
    std::memcpy(fixture.bytes + prefix.Size(), instruction.data(), instruction.size());
    const auto start = prefix.Size() + instruction.size();
    Assembler suffix(fixture.bytes + start, 4096 - start);
    // Take both observations before running any more guest arithmetic.
    suffix.Emit(ZYDIS_MNEMONIC_PUSHFQ);
    suffix.Emit(ZYDIS_MNEMONIC_POP, {Mem(ZYDIS_REGISTER_RCX, offsetof(State, flags), 8)});
    suffix.Emit(ZYDIS_MNEMONIC_STMXCSR, {Mem(ZYDIS_REGISTER_RCX, offsetof(State, mxcsr), 4)});
    MoveVectors(suffix, true);
    for (unsigned i = 0; i < 4; ++i)
        suffix.Emit(ZYDIS_MNEMONIC_MOV,
                    {Mem(ZYDIS_REGISTER_RCX, offsetof(State, general) + 8 * i, 8), Reg(Gpr(8 + i))});
    suffix.Emit(ZYDIS_MNEMONIC_RET);
    auto body = reinterpret_cast<Body>(fixture.bytes);
    fixture.Protect(PAGE_EXECUTE_READ);
    std::array<State, 4> expected{};
    for (unsigned p = 0; p < expected.size(); ++p) {
        expected[p] = Seed(p);
        Invoke(body, expected[p], p);
    }
    fixture.Protect(PAGE_READWRITE);
    Image image(Features{}, false);
    image.Prepare({fixture.bytes, start + suffix.Size()});
    if (image.Stats().avx != 9)
        throw std::runtime_error("Incomplete AVX fixture conversion: " + name);
    fixture.Protect(PAGE_EXECUTE_READ);
    trap = &image;
    for (unsigned p = 0; p < expected.size(); ++p) {
        auto got = Seed(p);
        Invoke(body, got, p);
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&got);
        const auto *want = reinterpret_cast<const std::uint8_t *>(&expected[p]);
        for (std::size_t i = 0; i < offsetof(State, mxcsr) + 4; ++i)
            if (bytes[i] != want[i]) {
                std::fprintf(stderr, "%s input %u byte %zu: native %02x, SSE2 %02x\n", name.c_str(), p, i,
                             want[i], bytes[i]);
                trap = nullptr;
                throw std::runtime_error("AVX differential mismatch");
            }
    }
    trap = nullptr;
}
void ReportedSequence(const Image *&trap) {
    Fixture fixture;
    // vxorps xmm0,xmm0,xmm0; vmovups [rip+disp32],xmm0 at the reported site.
    constexpr std::uint8_t sequence[] = {0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xf8, 0x11, 0x05, 0, 0, 0, 0, 0xc3};
    std::memcpy(fixture.bytes, sequence, sizeof(sequence));
    auto *target = fixture.bytes + 128;
    const std::int32_t delta = 128 - 12;
    std::memcpy(fixture.bytes + 8, &delta, 4);
    std::memset(target, 0x5a, 17);
    Image image(Features{}, false);
    image.Prepare({fixture.bytes, sizeof(sequence)});
    trap = &image;
    fixture.Protect(PAGE_EXECUTE_READWRITE);
    reinterpret_cast<void (*)()>(fixture.bytes)();
    trap = nullptr;
    std::uint8_t zero[16]{};
    if (image.Stats().avx != 2 || std::memcmp(target, zero, 16) || target[16] != 0x5a)
        throw std::runtime_error("Reported AVX sequence");
}
std::vector<std::string> Words(const char *text) {
    std::istringstream input(text);
    std::vector<std::string> result;
    for (std::string word; input >> word;)
        result.push_back(std::move(word));
    return result;
}
struct Group {
    const char *operands;
    const char *names;
};
void MissingMetadataAndMaskedMemory() {
    // Simulate a real #UD at the end of an executable segment. Masked-off
    // elements must not touch the adjacent guard page, for either direction.
    auto *pages = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 8192, MEM_RESERVE, PAGE_NOACCESS));
    if (!pages || !VirtualAlloc(pages, 4096, MEM_COMMIT, PAGE_READWRITE))
        throw std::runtime_error("Guard page allocation");
    std::unique_ptr<void, decltype([](void *p) { VirtualFree(p, 0, MEM_RELEASE); })> release(pages);
    auto *last = pages + 4092;
    std::uint32_t source = 0x1234abcd;
    std::memcpy(last, &source, 4);
    for (bool store : {false, true}) {
        std::uint8_t bytes[15]{};
        Assembler code(bytes, sizeof(bytes));
        const auto xmm0 = Reg(ZYDIS_REGISTER_XMM0), mask = Reg(ZYDIS_REGISTER_XMM1),
                   memory = Mem(ZYDIS_REGISTER_RCX, 0, 16);
        code.Emit(ZYDIS_MNEMONIC_VMASKMOVPS, {store ? memory : xmm0, mask, store ? xmm0 : memory},
                  ZYDIS_ENCODABLE_ENCODING_VEX);
        CONTEXT c{};
        c.Rip = reinterpret_cast<std::uintptr_t>(bytes);
        c.Rcx = reinterpret_cast<std::uintptr_t>(last);
        c.FltSave.XmmRegisters[1].Low = 0x80000000;
        c.FltSave.XmmRegisters[0].Low = 0x55;
        c.MxCsr = 0x1f80;
        c.EFlags = 0x247;
        Image image(Features{}, false);
        if (image.HandleUnlistedVector(c, {bytes, code.Size() - 1}) ||
            !image.HandleUnlistedVector(c, {bytes, code.Size()}) ||
            c.Rip != reinterpret_cast<std::uintptr_t>(bytes) + code.Size() || c.EFlags != 0x247 ||
            (store ? *last != 0x55 : c.FltSave.XmmRegisters[0].Low != source))
            throw std::runtime_error("Guarded AVX masked memory or unlisted instruction");
    }
}
void ThreadVectors(const Image *&trap) {
    Fixture fixture;
    Assembler code(fixture.bytes, 4096);
    code.Emit(ZYDIS_MNEMONIC_VMOVUPS, {Reg(ZYDIS_REGISTER_YMM0), Mem(ZYDIS_REGISTER_RCX, 0, 32)},
              ZYDIS_ENCODABLE_ENCODING_VEX);
    code.Emit(ZYDIS_MNEMONIC_RET);
    const auto extract = code.Size();
    code.Emit(ZYDIS_MNEMONIC_VEXTRACTF128, {Mem(ZYDIS_REGISTER_RCX, 0, 16), Reg(ZYDIS_REGISTER_YMM0), Imm(1)},
              ZYDIS_ENCODABLE_ENCODING_VEX);
    code.Emit(ZYDIS_MNEMONIC_RET);
    Image image(Features{}, false);
    image.Prepare({fixture.bytes, code.Size()});
    fixture.Protect(PAGE_EXECUTE_READ);
    trap = &image;
    std::barrier point(2);
    std::array<bool, 2> pass{};
    const auto worker = [&](unsigned n) {
        std::array<std::uint64_t, 4> guest{0, 0, n + 17, n + 43};
        std::array<std::uint64_t, 2> result{};
        reinterpret_cast<void (*)(const void *)>(fixture.bytes)(guest.data());
        point.arrive_and_wait();
        reinterpret_cast<void (*)(void *)>(fixture.bytes + extract)(result.data());
        pass[n] = result[0] == guest[2] && result[1] == guest[3];
    };
    std::thread first(worker, 0), second(worker, 1);
    first.join();
    second.join();
    trap = nullptr;
    if (!pass[0] || !pass[1])
        throw std::runtime_error("Thread-local YMM high halves");
}
} // namespace
void VectorTests(const Image *&trap) {
    ReportedSequence(trap);
    MissingMetadataAndMaskedMemory();
    ThreadVectors(trap);
    if (!DetectFeatures().avx)
        return; // The differential oracle needs hardware AVX.
    // Each slash exercises both a register operand and an exactly-sized memory operand.
    // Operands are synthetic and independent of any original game image.
    static constexpr Group groups[] = {
        {"16 x x", "vmaskmovps"},
        {"32 y y", "vmaskmovpd vmaskmovps"},
        {"4 x", "vmovss"},
        {"8 x", "vmovlpd vmovlps vmovsd"},
        {"g x", "vmovmskpd vmovmskps vpmovmskb"},
        {"g x i", "vpextrw"},
        {"g x/4", "vcvttss2si"},
        {"g x/8", "vcvttsd2si"},
        {"g y", "vmovmskps"},
        {"g/1 x i", "vpextrb"},
        {"g/4 x", "vmovd"},
        {"g/4 x i", "vextractps vpextrd"},
        {"q x/4", "vcvttss2si"},
        {"q x/8", "vcvttsd2si"},
        {"q/8 x", "vmovq"},
        {"q/8 x i", "vpextrq"},
        {"x 4", "vbroadcastss vmovss"},
        {"x 8", "vmovsd"},
        {"x g/4", "vmovd"},
        {"x q/8", "vmovq"},
        {"x x 16", "vmaskmovps"},
        {"x x 8", "vmovhps vmovlps"},
        {"x x g/1 i", "vpinsrb"},
        {"x x g/2 i", "vpinsrw"},
        {"x x g/4", "vcvtsi2sd vcvtsi2ss"},
        {"x x g/4 i", "vpinsrd"},
        {"x x i", "vpslld vpslldq vpsllq vpsllw vpsrad vpsraw vpsrld vpsrldq vpsrlq vpsrlw"},
        {"x x q/8", "vcvtsi2sd vcvtsi2ss"},
        {"x x q/8 i", "vpinsrq"},
        {"x x x", "vmovlhps vmovss"},
        {"x x x/16",
         "vaddps vandnpd vandnps vandpd vandps vdivpd vdivps vhaddpd vhaddps vmaxps vminps vmulpd vmulps vorpd "
         "vorps vpackssdw vpacksswb vpackusdw vpackuswb vpaddb vpaddd vpaddq vpaddw vpand vpandn vpcmpeqb "
         "vpcmpeqd vpcmpeqq vpcmpgtb vpcmpgtd vpcmpgtq vphaddd vpmaddwd vpmaxsd vpmaxub vpmaxud vpminsd "
         "vpminud vpmulld vpmuludq vpor vpshufb vpsllq vpsrad vpsubd vpsubq vpsubusw vpsubw vpunpckhbw "
         "vpunpckhdq vpunpckhqdq vpunpckhwd vpunpcklbw vpunpckldq vpunpcklqdq vpunpcklwd vpxor vsubpd vsubps "
         "vunpckhpd vunpckhps vunpcklpd vunpcklps vxorpd vxorps"},
        {"x x x/16 i", "vblendpd vblendps vcmpps vpblendw vshufps"},
        {"x x x/16 x", "vblendvpd vblendvps vpblendvb"},
        {"x x x/4", "vaddss vcvtss2sd vdivss vmaxss vminss vmulss vsqrtss vsubss"},
        {"x x x/4 i", "vcmpss vinsertps vroundss"},
        {"x x x/8", "vaddsd vcvtsd2ss vdivsd vmaxsd vminsd vmulsd vsubsd"},
        {"x x x/8 i", "vcmpsd vroundsd"},
        {"x x/16", "vcvtdq2ps vcvtpd2ps vcvttpd2dq vcvttps2dq vmovapd vmovaps vmovdqa vmovdqu vmovshdup "
                   "vmovsldup vmovupd vmovups vpabsd vptest vrcpps vrsqrtps vsqrtps"},
        {"x x/16 i", "vpermilpd vpermilps vpshufd vpshufhw vpshuflw vroundps"},
        {"x x/2", "vpmovsxbq vpmovzxbq"},
        {"x x/4", "vpmovsxbd vpmovsxwq vpmovzxbd vucomiss"},
        {"x x/8", "vcvtdq2pd vmovddup vmovq vpmovsxdq vpmovsxwd vpmovzxbw vpmovzxdq vpmovzxwd vucomisd"},
        {"x y/32", "vcvtpd2ps vcvttpd2dq"},
        {"x/16 x", "vmovapd vmovaps vmovdqa vmovdqu vmovupd vmovups"},
        {"x/16 y i", "vextractf128"},
        {"x/8 x", "vmovq"},
        {"y 16", "vbroadcastf128"},
        {"y 4", "vbroadcastss"},
        {"y 8", "vbroadcastsd"},
        {"y x/16", "vcvtdq2pd"},
        {"y y 32", "vmaskmovps"},
        {"y y x/16 i", "vinsertf128"},
        {"y y y/32", "vaddpd vaddps vandnps vandpd vandps vdivpd vdivps vmulpd vmulps vorpd vorps vpermilps "
                     "vsubpd vsubps vunpckhpd vunpckhps vunpcklpd vunpcklps vxorpd vxorps"},
        {"y y y/32 i", "vblendpd vblendps vcmppd vcmpps vperm2f128 vshufpd vshufps"},
        {"y y y/32 y", "vblendvpd vblendvps"},
        {"y y/32",
         "vcvtdq2ps vcvttps2dq vmovapd vmovaps vmovddup vmovdqa vmovdqu vmovshdup vmovupd vmovups vptest"},
        {"y y/32 i", "vpermilpd vpermilps"},
        {"y/32 y", "vmovapd vmovaps vmovdqa vmovdqu vmovupd vmovups"},
    };
    unsigned cases{}, failures{};
    for (const auto &group : groups)
        for (const auto &name : Words(group.names)) {
            ZydisEncoderRequest request{};
            request.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
            request.allowed_encodings = ZYDIS_ENCODABLE_ENCODING_VEX;
            for (int m = 1; m <= ZYDIS_MNEMONIC_MAX_VALUE; ++m)
                if (name == ZydisMnemonicGetString(static_cast<ZydisMnemonic>(m))) {
                    request.mnemonic = static_cast<ZydisMnemonic>(m);
                    break;
                }
            const auto args = Words(group.operands);
            request.operand_count = static_cast<ZyanU8>(args.size());
            const bool alternatives = std::string_view(group.operands).find('/') != std::string_view::npos;
            unsigned valid{};
            for (unsigned mem = 0; mem <= (alternatives ? 1u : 0u); ++mem) {
                for (unsigned j = 0; j < args.size(); ++j) {
                    auto arg = args[j];
                    auto slash = arg.find('/');
                    if (slash != arg.npos)
                        arg = mem ? arg.substr(slash + 1) : arg.substr(0, slash);
                    if (arg == "i")
                        request.operands[j] = Imm(0);
                    else if (arg == "x" || arg == "y")
                        request.operands[j] = Reg(static_cast<ZydisRegister>(
                            (arg == "x" ? ZYDIS_REGISTER_XMM0 : ZYDIS_REGISTER_YMM0) + j));
                    else if (arg == "g" || arg == "q")
                        request.operands[j] = Reg(static_cast<ZydisRegister>(
                            (arg == "g" ? ZYDIS_REGISTER_R8D : ZYDIS_REGISTER_R8) + j));
                    else
                        request.operands[j] = Mem(ZYDIS_REGISTER_RCX, offsetof(State, memory), std::stoi(arg));
                    if (j == 3 && request.operands[j].type == ZYDIS_OPERAND_TYPE_REGISTER)
                        request.operands[j].reg.is4 = ZYAN_TRUE;
                }
                std::vector<unsigned> selectors{0};
                if (args.back() == "i") {
                    selectors = {0, 1, 7, 8, 0x54, 0x9a, 0xff};
                    if (name.starts_with("vcmp")) {
                        selectors.clear();
                        for (unsigned predicate = 0; predicate < 32; ++predicate)
                            selectors.push_back(predicate);
                    }
                }
                for (const auto selector : selectors) {
                    if (args.back() == "i")
                        request.operands[args.size() - 1] = Imm(selector);
                    std::uint8_t bytes[15]{};
                    std::size_t n = sizeof(bytes);
                    if (!ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&request, bytes, &n)))
                        continue;
                    try {
                        CheckCase(trap, {bytes, n},
                                  name + " " + group.operands + (mem ? " mem" : " reg") + " imm " +
                                      std::to_string(selector));
                    } catch (const std::exception &error) {
                        std::fprintf(stderr, "%s: %s\n", name.c_str(), error.what());
                        trap = nullptr;
                        ++failures;
                    }
                    ++valid;
                    ++cases;
                }
            }
            if (!valid) {
                std::fprintf(stderr, "No valid test encoding for %s %s\n", name.c_str(), group.operands);
                ++failures;
            }
        }
    std::printf("AVX: %u encodings compared against native hardware on four inputs, %u failures\n", cases,
                failures);
    if (failures)
        throw std::runtime_error("AVX differential failed");
}
