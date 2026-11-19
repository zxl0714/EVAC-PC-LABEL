#include "hosted_graphics_info.h"
#include <Windows.h>
#include <Psapi.h>
#include <winver.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cave::ps2 {
namespace {
// Read only mapped image metadata. Discarded debug pages are optional.
struct MappedImage {
    uintptr_t base;
    DWORD size;

    bool Read(uintptr_t rva, void* destination, size_t bytes) const noexcept {
        SIZE_T copied{};
        return rva < size && bytes <= size - rva &&
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + rva),
                              destination, bytes, &copied) && copied == bytes;
    }
};

void ReadPdbId(const MappedImage& image, const IMAGE_NT_HEADERS64& nt, char (&id)[64]) noexcept {
    if (nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) return;
    const auto& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!directory.VirtualAddress) return;
    struct CodeView { DWORD signature; GUID guid; DWORD age; };
    static_assert(sizeof(CodeView) == 24);
    for (size_t i = 0; i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY) && i < 64; ++i) {
        IMAGE_DEBUG_DIRECTORY debug{};
        if (!image.Read(directory.VirtualAddress + i * sizeof(debug), &debug, sizeof(debug))) return;
        if (debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || !debug.AddressOfRawData || debug.SizeOfData < sizeof(CodeView)) continue;
        CodeView data{};
        if (!image.Read(debug.AddressOfRawData, &data, sizeof(data)) || data.signature != 0x53445352) continue; // RSDS
        const auto& g = data.guid;
        std::snprintf(id, sizeof(id), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x/%lu",
            g.Data1, unsigned(g.Data2), unsigned(g.Data3), unsigned(g.Data4[0]), unsigned(g.Data4[1]),
            unsigned(g.Data4[2]), unsigned(g.Data4[3]), unsigned(g.Data4[4]), unsigned(g.Data4[5]),
            unsigned(g.Data4[6]), unsigned(g.Data4[7]), data.age);
        return;
    }
}

// An x64 loader export may be a linker jump to the function. Accept only an
// exact unwind entry in this image. External hooks and unknown thunks stay unknown.
void LogExportRange(const MappedImage& image, HMODULE module, const char* name) noexcept {
    auto address = reinterpret_cast<uintptr_t>(GetProcAddress(module, name));
    std::array<BYTE, 5> entry{};
    if (!image.Read(address - image.base, entry.data(), entry.size())) return;
    if (entry[0] == 0xe9) {
        int32_t displacement{};
        std::memcpy(&displacement, entry.data() + 1, sizeof(displacement));
        address = address + entry.size() + displacement;
    }
    if (address - image.base >= image.size) return;
    DWORD64 base{};
    const auto* range = RtlLookupFunctionEntry(address, &base, nullptr);
    if (range && base == image.base && range->BeginAddress == address - base &&
        range->EndAddress > range->BeginAddress && range->EndAddress <= image.size)
        std::fprintf(stderr, "[ps2-dl-wsi] loader_export=%s begin_rva=0x%lx end_rva=0x%lx\n",
                     name, range->BeginAddress, range->EndAddress);
}

void LogImage(HMODULE module, const char* role, bool loader) noexcept {
    MODULEINFO info{};
    if (!module || !K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return;
    const MappedImage image{reinterpret_cast<uintptr_t>(info.lpBaseOfDll), info.SizeOfImage};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!image.Read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        !image.Read(static_cast<DWORD>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return;
    char id[64] = "unavailable";
    ReadPdbId(image, nt, id);
    std::fprintf(stderr, "[ps2-dl-wsi] image=%s timestamp=%08lx size=%lu pdb=%s\n",
                 role, nt.FileHeader.TimeDateStamp, image.size, id);
    if (loader) {
        LogExportRange(image, module, "vkCmdPipelineBarrier");
        LogExportRange(image, module, "vkCreateSwapchainKHR");
        LogExportRange(image, module, "vkQueuePresentKHR");
    }
}
}

void LogHostedGraphicsImages() noexcept {
    LogImage(reinterpret_cast<HMODULE>(&__ImageBase), "runtime", false);
    // Volk holds the loader for the lifetime of this device.
    LogImage(GetModuleHandleW(L"vulkan-1.dll"), "vulkan-1.dll", true);
    std::fflush(stderr);
}

void ReadRtssVersion(char (&value)[48]) noexcept {
    HMODULE module{};
    if (!GetModuleHandleExW(0, L"RTSSHooks64.dll", &module)) {
        std::snprintf(value, sizeof(value), "not-loaded");
        return;
    }
    wchar_t path[1024]{};
    const auto length = GetModuleFileNameW(module, path, DWORD(std::size(path)));
    FreeLibrary(module);
    std::snprintf(value, sizeof(value), "loaded-version-unknown");
    if (!length || length >= std::size(path)) return;
    const auto size = GetFileVersionInfoSizeW(path, nullptr);
    if (!size) return;
    std::unique_ptr<BYTE[]> bytes{new (std::nothrow) BYTE[size]};
    if (!bytes || !GetFileVersionInfoW(path, 0, size, bytes.get())) return;
    void* block{};
    UINT blockSize{};
    if (!VerQueryValueW(bytes.get(), L"\\", &block, &blockSize) || blockSize < sizeof(VS_FIXEDFILEINFO)) return;
    const auto& info = *static_cast<const VS_FIXEDFILEINFO*>(block);
    if (info.dwSignature == VS_FFI_SIGNATURE)
        std::snprintf(value, sizeof(value), "%u.%u.%u.%u", unsigned(HIWORD(info.dwFileVersionMS)),
            unsigned(LOWORD(info.dwFileVersionMS)), unsigned(HIWORD(info.dwFileVersionLS)), unsigned(LOWORD(info.dwFileVersionLS)));
}
}
