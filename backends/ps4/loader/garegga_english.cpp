#include "garegga_english.h"
#include "garegga_english_data.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace cave::ps4 {
namespace {

void Require(bool ok, const char* detail) {
    if (!ok) throw std::runtime_error(detail);
}

class CaptionCanvas {
public:
    explicit CaptionCanvas(const GareggaCaption& caption) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(caption.width);
        info.bmiHeader.biHeight = -static_cast<LONG>(caption.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        if (dc_) bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
        if (!bitmap_) {
            if (dc_) DeleteDC(dc_);
            throw std::runtime_error("Cannot allocate the Garegga caption bitmap");
        }
        SelectObject(dc_, bitmap_);
        SetBkMode(dc_, TRANSPARENT);
        SetTextAlign(dc_, TA_LEFT | TA_BASELINE);
        SetTextColor(dc_, RGB(255, 255, 255));
        std::memset(pixels_, 0, caption.width * caption.height * 4);
    }

    ~CaptionCanvas() {
        DeleteDC(dc_);
        DeleteObject(bitmap_);
    }

    CaptionCanvas(const CaptionCanvas&) = delete;
    CaptionCanvas& operator=(const CaptionCanvas&) = delete;

    std::span<const std::uint8_t> Draw(const GareggaCaption& caption) const {
        std::vector<std::wstring_view> lines;
        auto remaining = caption.text;
        while (!remaining.empty()) {
            const auto split = remaining.find(L'\n');
            lines.push_back(remaining.substr(0, split));
            remaining.remove_prefix(split == remaining.npos ? remaining.size() : split + 1);
        }
        const auto rows = static_cast<int>(lines.size());
        const auto width = static_cast<int>(caption.width);
        const auto height = static_cast<int>(caption.height);
        const auto index = [](int row) { return static_cast<std::size_t>(row); };
        std::vector<SIZE> sizes(lines.size());
        constexpr MAT2 identity{{0, 1}, {}, {}, {0, 1}};
        // Fit the ink and descenders, since Windows line metrics also reserve
        // space for accents that most menu captions do not use.
        for (int size = height * 5 / (4 * rows); size >= 6; --size) {
            const auto font = CreateFontW(-size, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            Require(font != nullptr, "Cannot select the Garegga English font");
            const auto previous = SelectObject(dc_, font);
            bool fits = previous != nullptr && previous != HGDI_ERROR;
            int ascent{}, descent{};
            for (int i = 0; fits && i < rows; ++i) {
                const auto line = lines[index(i)];
                auto& extent = sizes[index(i)];
                fits = GetTextExtentPoint32W(dc_, line.data(), static_cast<int>(line.size()), &extent) &&
                    extent.cx <= width - 2;
                for (const auto ch : line) {
                    GLYPHMETRICS metrics{};
                    if (GetGlyphOutlineW(dc_, ch, GGO_METRICS, &metrics, 0, nullptr, &identity) == GDI_ERROR) {
                        fits = false;
                        break;
                    }
                    ascent = (std::max)(ascent, static_cast<int>(metrics.gmptGlyphOrigin.y));
                    descent = (std::max)(descent, static_cast<int>(metrics.gmBlackBoxY) -
                        static_cast<int>(metrics.gmptGlyphOrigin.y));
                }
            }
            const auto line_height = ascent + descent + 3;
            const auto total_height = line_height * rows - 3;
            if (fits && total_height <= height - 1) {
                for (int i = 0; i < rows; ++i) {
                    const auto line = lines[index(i)];
                    if (!TextOutW(dc_, caption.centered ? (width - sizes[index(i)].cx) / 2 : 0,
                            ascent + (height - total_height) / 2 + i * line_height,
                            line.data(), static_cast<int>(line.size()))) fits = false;
                }
            } else {
                fits = false;
            }
            if (previous && previous != HGDI_ERROR) SelectObject(dc_, previous);
            DeleteObject(font);
            if (fits) {
                GdiFlush();
                return {static_cast<const std::uint8_t*>(pixels_), caption.width * caption.height * 4};
            }
        }
        throw std::runtime_error("Garegga English caption does not fit its original sprite");
    }

private:
    HDC dc_{};
    HBITMAP bitmap_{};
    void* pixels_{};
};

// RGBA8_SW and A8L8_SW use 8x8 Morton-ordered tiles. Caption sprites are white ink
// over a transparent background. Their original animation and anchors stay intact.
std::size_t AtlasPixel(unsigned x, unsigned y, unsigned width) {
    unsigned morton{};
    for (unsigned i = 0; i < 3; ++i) {
        morton |= ((x >> i) & 1) << (i * 2);
        morton |= ((y >> i) & 1) << (i * 2 + 1);
    }
    return (y / 8 * (width / 8) + x / 8) * 64 + morton;
}
}

std::span<const GareggaAtlas> GareggaEnglishAtlases() {
    return garegga::kEnglishAtlases;
}

void ApplyGareggaEnglish(std::span<std::uint8_t> motion, const GareggaAtlas& atlas) {
    Require(atlas.channels == 4 || atlas.channels == 2, "Unsupported Garegga caption texture type");
    const auto texture_size = static_cast<std::size_t>(atlas.width) * atlas.height * atlas.channels;
    Require(atlas.texture_offset <= motion.size() && texture_size <= motion.size() - atlas.texture_offset,
        "Invalid Garegga texture extent");
    const auto texture = motion.subspan(atlas.texture_offset, texture_size);
    for (const auto& caption : atlas.captions) {
        const auto backdrop = atlas.replay_panel ? 8u : 0u;
        Require(caption.width && caption.height && !caption.text.empty() &&
            caption.x <= atlas.width && caption.width + backdrop <= atlas.width - caption.x &&
            caption.y <= atlas.height && caption.height <= atlas.height - caption.y,
            "Invalid Garegga caption extent");
        const CaptionCanvas canvas(caption);
        const auto raster = canvas.Draw(caption);
        for (unsigned y = 0; y < caption.height; ++y) {
            for (unsigned x = 0; x < caption.width; ++x) {
                const auto alpha = raster[(y * caption.width + x) * 4];
                const auto pixel = AtlasPixel(caption.x + x, caption.y + y, atlas.width) * atlas.channels;
                if (atlas.replay_panel) {
                    // Use the unlettered eight-pixel pattern immediately right
                    // of the caption and keep the panel, its border and icons.
                    const auto sample_x = caption.x + caption.width + ((x - caption.width) & 7);
                    const auto sample = AtlasPixel(sample_x, caption.y + y, atlas.width) * atlas.channels;
                    constexpr std::uint8_t ink[]{177, 148, 149};
                    for (unsigned c = 0; c < 3; ++c)
                        texture[pixel + c] = static_cast<std::uint8_t>(
                            (texture[sample + c] * (255u - alpha) + ink[c] * alpha + 127) / 255);
                } else {
                    std::fill_n(texture.data() + pixel, atlas.channels - 1, std::uint8_t{255});
                    texture[pixel + atlas.channels - 1] = alpha;
                }
            }
        }
    }
}
}
