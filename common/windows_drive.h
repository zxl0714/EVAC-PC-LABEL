#pragma once
#include <Windows.h>
#include <filesystem>

namespace cave {
inline bool RunningUnderWine() {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}

// Wine may report the mapped DOS drive root itself as a reparse point.
inline bool IsWineDriveRoot(const std::filesystem::path& path) {
    const auto& name = path.native();
    return name.size() == 3 && name[1] == L':' && (name[2] == L'\\' || name[2] == L'/') &&
           RunningUnderWine();
}
}
