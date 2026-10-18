// Bounded console disassembly. Reads bytes; never executes them.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
extern "C" {
#include <dis-asm.h>
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 6) throw std::runtime_error("Usage: ppc_disasm <dump> <base> <address> <count> <size>");
        const auto base = std::stoull(argv[2], nullptr, 0);
        const auto address = std::stoull(argv[3], nullptr, 0);
        const auto count = std::stoull(argv[4], nullptr, 0);
        const auto size = std::stoull(argv[5], nullptr, 0);
        const std::filesystem::path file(argv[1]);
        if (!count || count > 128 || base > address || address % 4 || size > 128 * 1024 * 1024 ||
            base + size > 0x100000000ull || address + count * 4 > base + size ||
            std::filesystem::file_size(file) != size)
            throw std::runtime_error("Invalid disassembly range or file size");
        std::ifstream input(file, std::ios::binary);
        input.exceptions(std::ios::badbit | std::ios::failbit);
        input.seekg(address - base);
        std::vector<unsigned char> bytes(count * 4);
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        disassemble_info info{};
        INIT_DISASSEMBLE_INFO(info, stdout, fprintf);
        info.arch = bfd_arch_powerpc;
        info.endian = BFD_ENDIAN_BIG;
        // Identical Xenon dialect to SDK src/codegen/ppc/disasm.cpp.
        constexpr std::uintptr_t dialect = 0x1 | 0x4 | 0x4000 | 0x8000000 | 0x200 | 0x1000000 | 0x10000;
        info.private_data = reinterpret_cast<void*>(dialect);
        info.buffer = bytes.data();
        info.buffer_vma = address;
        info.buffer_length = bytes.size();
        for (std::uint64_t offset = 0; offset < bytes.size(); offset += 4) {
            std::printf("%08llX  %02X%02X%02X%02X  ", address + offset, bytes[offset], bytes[offset+1],
                        bytes[offset+2], bytes[offset+3]);
            if (print_insn_big_powerpc(address + offset, &info) != 4)
                throw std::runtime_error("Instruction decode failed");
            std::putchar('\n');
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
