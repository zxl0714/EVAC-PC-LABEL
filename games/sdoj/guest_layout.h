#pragma once

#include <cstdint>

namespace sdoj {
#if defined(SDOJ_TITLE_UPDATE) && SDOJ_TITLE_UPDATE
inline constexpr bool kTitleUpdate = true;
inline constexpr wchar_t kNormalModule[] = L"sdoj_tu101_CA022100.dll";
inline constexpr wchar_t kArrangeModule[] = L"sdoj_tu101_CA022110.dll";
#else
inline constexpr bool kTitleUpdate = false;
inline constexpr wchar_t kNormalModule[] = L"sdoj_CA022100.dll";
inline constexpr wchar_t kArrangeModule[] = L"sdoj_CA022110.dll";
#endif

inline constexpr std::uint32_t kFrameCounter = kTitleUpdate ? 0x82633748 : 0x82633548;
inline constexpr std::uint32_t kCallbackFlag = kTitleUpdate ? 0x8279C970 : 0x8279C770;
inline constexpr std::uint32_t kPendingFramesCallback = kTitleUpdate ? 0x8219EB18 : 0x8219DE60;

// The two gameplay overlays insert 32 bytes before their mutable render and
// input state. Absolute code positions are mapped separately for each overlay.
inline constexpr std::uint32_t OverlayData(std::uint32_t base) {
    return base + (kTitleUpdate ? 0x20 : 0);
}
}
