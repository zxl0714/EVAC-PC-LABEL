#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cave::ps4::cpu {
struct Features {
    bool bmi1{}, bmi2{}, lzcnt{}, avx{}, f16c{}, ssse3{}, sse41{}, movbe{};
};
Features DetectFeatures();
void ConfigureForTests(bool force);
bool ForcedForTests();

enum class Operation { Bextr, Andn, Blsi, Blsmsk, Blsr, Lzcnt, Tzcnt, Movbe, Count };
struct Frame { std::array<std::uint64_t,16> registers; std::uint64_t flags; };
static_assert(sizeof(Frame)==136);
struct Operand {
    int reg{-1}, base{-1}, index{-1};
    unsigned scale{}, address_bits{64};
    std::int64_t displacement{};
    bool memory{}, rip_relative{};
};
struct Instruction {
    Operation operation{};
    unsigned bits{}, destination{}, control{}, length{};
    Operand source;
    std::uint64_t next{};
    bool store{};
};
// Pure guest semantics; shared by generated adapters and the short-instruction
// exception path. The latter reads memory separately before committing context.
std::uint64_t Evaluate(Operation operation,unsigned bits,std::uint64_t source,
                       std::uint64_t control,std::uint64_t& flags);
struct Statistics {
    std::array<std::size_t,static_cast<std::size_t>(Operation::Count)> converted{};
    std::size_t trampolines{}, short_traps{}, decoded{};
};

class Image {
public:
    explicit Image(Features features=DetectFeatures(),bool force=ForcedForTests());
    ~Image();
    Image(const Image&)=delete;
    Image& operator=(const Image&)=delete;
    // Called only while a newly loaded guest image is private, writable and
    // unentered. The caller supplies validated function bodies: executable
    // segments can also contain ROM/resource data and are not safe boundaries.
    void Prepare(std::span<std::uint8_t> code);
    bool HandleException(EXCEPTION_POINTERS* exception) const;
    const Statistics& Stats() const;
    bool Needed() const;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
}
