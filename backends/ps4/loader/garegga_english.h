#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace cave { class Ini; }

namespace cave::ps4 {
struct GareggaCaption {
    unsigned x, y, width, height;
    bool centered;
    std::wstring_view text;
};

struct GareggaAtlas {
    std::string_view path, sha256;
    unsigned texture_offset, width, height, channels;
    std::span<const GareggaCaption> captions;
    bool replay_panel = false;
};

std::span<const GareggaAtlas> GareggaEnglishAtlases();
void ApplyGareggaEnglish(std::span<std::uint8_t> motion, const GareggaAtlas& atlas);
bool GareggaEnglishTranslationEnabled(const Ini& ini);
}
