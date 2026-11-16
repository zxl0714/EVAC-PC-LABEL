#pragma once
#include "ps2_runtime.h"
#include <stdexcept>

extern const uint32_t ps2_daioujou_death_tableBase, ps2_daioujou_death_tableSlotCount;
extern const uint32_t ps2_daioujou_gallery_tableBase, ps2_daioujou_gallery_tableSlotCount;
extern PS2Runtime::RecompiledFunction ps2_daioujou_death_table[], ps2_daioujou_gallery_table[];

namespace cave::daioujou_ps2 {
inline PS2Runtime::RecompiledProgram ProgramTable(std::string_view prepared) {
    if (prepared == "boot.elf")
        return {g_ps2RecompiledFunctionTableBase, {g_ps2RecompiledFunctionTable, g_ps2RecompiledFunctionTableSlotCount}};
    if (prepared == "death.elf")
        return {ps2_daioujou_death_tableBase, {ps2_daioujou_death_table, ps2_daioujou_death_tableSlotCount}};
    if (prepared == "gallery.elf")
        return {ps2_daioujou_gallery_tableBase, {ps2_daioujou_gallery_table, ps2_daioujou_gallery_tableSlotCount}};
    throw std::invalid_argument("Unknown DaiOuJou PS2 executable");
}

inline std::string_view ResolveDiscExecutable(std::string_view path) {
    constexpr std::string_view prefix = "cdrom0:\\";
    if (!path.starts_with(prefix)) return {};
    path.remove_prefix(prefix.size());
    if (path.ends_with(";1")) path.remove_suffix(2);
    if (path == "SLPS_252.33") return "boot.elf";
    if (path == "DEATH.ELF") return "death.elf";
    if (path == "GALLERY.ELF") return "gallery.elf";
    return {};
}
}
