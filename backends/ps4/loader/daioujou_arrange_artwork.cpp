#include "daioujou_arrange.h"
#include "daioujou_arrange_artwork_data.h"
#include "backends/ps4/content/resource_prepare.h"
#include "common/death_label_gadgets.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::string_view gadgetBaseHash = "829d61051eea451a90a8998689e16b2a7c0c2a85d4530fd0dda93b6eaacbc258";
constexpr std::string_view gadgetUpdateHash = "f522e1c2f11e290efbff3492c3b68e7928c9dedc5b3848989cc2851a73625bce";
constexpr std::size_t GadgetTextureOffset(bool update) { return update ? 0x9e860 : 0x92fc0; }

void Check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Cannot decode PS4 menu artwork");
}

class ComApartment {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        if (result_ != RPC_E_CHANGED_MODE) Check(result_);
    }
    ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
private:
    HRESULT result_;
};

struct BadgeRegion { unsigned x, y, width, height; };

std::vector<std::uint8_t> DecodeBadge(IWICImagingFactory& factory, std::span<const std::uint8_t> png,
                                      const BadgeRegion& badge) {
    std::vector<BYTE> source(png.begin(), png.end());
    ComPtr<IWICStream> stream;
    Check(factory.CreateStream(stream.GetAddressOf()));
    Check(stream->InitializeFromMemory(source.data(), static_cast<DWORD>(source.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory.CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, frame.GetAddressOf()));
    unsigned width{}, height{};
    Check(frame->GetSize(&width, &height));
    if (width != badge.width || height != badge.height) throw std::runtime_error("Unexpected PS4 menu artwork dimensions");
    ComPtr<IWICFormatConverter> converter;
    Check(factory.CreateFormatConverter(converter.GetAddressOf()));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<std::uint8_t> rgba(width * height * 4);
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()));
    return rgba;
}

// PS4 UI atlases store pixels as 8x8 Morton-ordered tiles.
std::size_t AtlasPixel(unsigned x, unsigned y, unsigned tiles_per_row = 512) {
    unsigned inside{};
    for (unsigned i = 0; i < 3; ++i) {
        inside |= ((x >> i) & 1) << (i * 2);
        inside |= ((y >> i) & 1) << (i * 2 + 1);
    }
    return ((y / 8 * tiles_per_row + x / 8) * 64 + inside) * 4;
}

void PaintBadge(std::span<std::uint8_t> atlas, IWICImagingFactory& factory,
                std::span<const std::uint8_t> png, BadgeRegion badge) {
    const auto rgba = DecodeBadge(factory, png, badge);
    using Pixel = std::array<int, 4>;
    std::vector<Pixel> original(badge.width * badge.height);
    const auto stock = [&](unsigned x, unsigned y) -> Pixel& { return original[y * badge.width + x]; };
    for (unsigned y = 0; y < badge.height; ++y) {
        for (unsigned x = 0; x < badge.width; ++x)
            std::copy_n(atlas.data() + AtlasPixel(badge.x + x, badge.y + y), 4, stock(x, y).begin());
    }

    const unsigned text_begin = badge.width / 5 + 6, text_end = badge.width - 10;
    for (unsigned y = 1; y + 1 < badge.height; ++y) {
        for (unsigned x = 1; x + 1 < badge.width; ++x) {
            auto background = stock(x, y);
            const bool label = y >= 2 && y + 2 < badge.height && x >= text_begin && x < text_end;
            if (label && badge.width == 264) {
                // Four blank rows between the old lines retain the stock
                // pattern phase and the horizontal gradient at each column.
                background = stock(x, 26 + (y + 2) % 4);
            } else if (label) {
                // The smaller badge uses a resampled 20-pixel period. Its
                // blank middle row supplies the gradient, and matching
                // columns beside the old label supply the row's dot phase.
                const unsigned left = 28 + (x - 28) % 20;
                const unsigned right = 169 + (x + 11) % 20;
                background = stock(x, 20);
                for (unsigned channel = 0; channel < 3; ++channel) {
                    int delta = stock(left, y)[channel] - stock(left, 20)[channel];
                    if (right <= 182) {
                        const int right_delta = stock(right, y)[channel] - stock(right, 20)[channel];
                        delta = (delta * static_cast<int>(right - x) + right_delta * static_cast<int>(x - left)) /
                            static_cast<int>(right - left);
                    }
                    background[channel] = std::clamp(background[channel] + delta, 0, 255);
                }
            }

            // Change the note's green contribution and its background tint
            // together. Preserve neutral dots and the stock border.
            const int tint = (std::max)(0, background[1] - background[0] - 2);
            background[0] = (std::min)(255, background[0] + (7 * tint + 4) / 8);
            background[1] = (std::max)(0, background[1] - (9 * tint + 4) / 8);
            background[2] = (std::min)(255, background[2] + tint + (tint + 2) / 4);
            if (label) {
                const auto color = rgba.data() + (y * badge.width + x) * 4;
                const int alpha = color[3];
                for (unsigned channel = 0; channel < 3; ++channel)
                    background[channel] = (color[channel] * alpha + background[channel] * (255 - alpha) + 127) / 255;
            }
            auto output = atlas.data() + AtlasPixel(badge.x + x, badge.y + y);
            for (unsigned channel = 0; channel < 4; ++channel)
                output[channel] = static_cast<std::uint8_t>(background[channel]);
        }
    }
}

void PaintKetsuiBadge(std::span<std::uint8_t> atlas, IWICImagingFactory& factory,
                      std::span<const std::uint8_t> png, BadgeRegion region, bool card) {
    const auto caption = DecodeBadge(factory, png, region);
    const auto pixel_at = [&](unsigned x, unsigned y) {
        return atlas.data() + AtlasPixel(region.x + x, region.y + y);
    };
    // Rebuild the fill across the entire colored plate. The original alpha
    // defines its beveled outline and the left BGM SET label stays intact.
    const unsigned start = card ? 42 : 1, end = region.width - 1;
    for (unsigned x = start; x < end; ++x) {
        const unsigned column = (x - start) * 32 / (end - start);
        const unsigned right_edge = (std::min)(end - x, (end - start) / 4);
        const unsigned left_edge = (std::min)(x - start, (end - start) / 4);
        const unsigned edge = (std::min)(left_edge, right_edge) * 48 / ((end - start) / 4);
        constexpr std::array bars{1u, 4u, 9u, 5u, 12u, 7u, 3u, 8u, 15u, 9u, 6u, 3u, 10u, 5u, 13u, 6u,
            4u, 8u, 5u, 11u, 7u, 14u, 8u, 4u, 8u, 11u, 5u, 8u, 16u, 10u, 4u, 2u};
        for (unsigned y = 1; y + 1 < region.height; ++y) {
            auto* pixel = pixel_at(x, y);
            const auto* ink = caption.data() + (y * region.width + x) * 4;
            if (!pixel[3]) continue;
            const unsigned level = y * 64 / region.height;
            const unsigned light = (std::min)(level, (64u - level) * 2);
            const unsigned ridge = (std::min)(y, region.height - 1 - y);
            const unsigned pattern = ((x - start) * 64 / (end - start)) % 2 == 0 &&
                56u - bars[column] <= level && level <= 57u ? 12u : 0u;
            const unsigned tone = (26 + edge + light + pattern) * (std::min)(ridge, 5u) / 5;
            const std::array background{12u + tone * 3 / 4, 16u + tone, 4u + tone / 5};
            const unsigned alpha = ink[3];
            for (unsigned channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<std::uint8_t>((background[channel] * (255 - alpha) + ink[2 - channel] * alpha + 127) / 255);
        }
    }
}
}

std::vector<std::uint8_t> BuildDaioujouDeathLabelArtwork() {
    constexpr unsigned width = 296, height = 104, atlas_width = 512, atlas_height = 128;
    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    const auto rgba = DecodeBadge(*factory.Get(), artwork::kDaioujouDeathLabel, {0, 0, width, height});
    // Native red cards use a one-pixel frame and four-pixel color accent.
    constexpr std::array<std::uint8_t, 4> frame{155, 141, 137, 255};
    constexpr std::array<std::uint8_t, 4> accent{210, 56, 56, 255};
    std::vector<std::uint8_t> atlas(atlas_width * atlas_height * 4);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto* pixel = rgba.data() + (y * width + x) * 4;
            if (x == 0 || x + 1 == width || y == 0 || y + 1 == height) pixel = frame.data();
            else if (x <= 4) pixel = accent.data();
            auto* output = atlas.data() + AtlasPixel(x, y, atlas_width / 8);
            output[0] = pixel[2];
            output[1] = pixel[1];
            output[2] = pixel[0];
            output[3] = pixel[3];
        }
    }

    // One static sprite with the same origin and dimensions as the original mode cards.
    const auto motion = nlohmann::json::parse(R"json({
        "id":"motion","spec":"ps4","version":3.03,"label":"evac_death_label",
        "screenSize":{"height":1024,"width":1280,"originX":0,"originY":0},
        "easing":[],"metadata":null,
        "object":{"evac_death_label":{"metadata":null,"type":0,"motion":{"stand":{
            "bounds":{"bottom":0,"left":0,"right":0,"top":0},
            "lastTime":1,"loopTime":-1,"metadata":null,
            "layer":[{
                "children":[],"coordinate":0,"exportSelf":1,"label":"card","type":0,
                "groundCorrection":0,"inheritMask":33556476,"joinTarget":1,"meshCombine":0,
                "meshTransform":0,"metadata":null,"objTriPriority":2,"parameterize":null,
                "stencilType":0,"transformOrder":[0,3,2,1],
                "frameList":[{"content":{"icon":"card","mask":0,"src":"tex#000"},"time":0,"type":2},
                             {"time":1,"type":0}]
            }],
            "layerIndexMap":{"card":0},"parameter":[],"parameterize":null,
            "priority":[{"content":[0],"time":0,"type":1},{"content":null,"time":1,"type":1}],
            "referenceModelFileList":[],"referenceProjectFileList":[],"tag":[],"type":0,"variable":[]
        }}}},
        "source":{"tex#000":{"icon":{"card":{"attr":2,"height":104,"left":0.0,"metadata":null,
            "originX":148,"originY":52,"top":0.0,"width":296}},
            "metadata":null,"texture":{"height":128,"pixel":{"resource_index":0},"type":"RGBA8_SW","width":512},
            "type":0}}
    })json");
    return WriteMotionPsb(motion, atlas);
}

void ApplyDaioujouArrangeArtwork(std::span<std::uint8_t> motion, bool update) {
    constexpr std::string_view base_hash = "fedf6e8aa14f4168afb74647a565e39605d81cedf71923ec1b866f971ffc8dad";
    constexpr std::string_view update_hash = "76af81f9b714fdf3eabada82431f4ace74ba90cf89a3d3f8d59415af54980587";
    if (PreparationHash(motion) != (update ? update_hash : base_hash))
        throw std::runtime_error("DaiOuJou common UI differs from the verified atlas");

    // Update the two unused BGM badges in the verified PSB.
    constexpr auto atlas_bytes = 4096u * 2048u * 4u;
    auto atlas = motion.subspan(update ? 0x51a00 : 0x50ce0, atlas_bytes);
    const BadgeRegion medium = update ? BadgeRegion{3905, 885, 184, 40} : BadgeRegion{2698, 1534, 184, 40};
    const BadgeRegion large = update ? BadgeRegion{2807, 485, 264, 56} : BadgeRegion{1731, 467, 264, 56};
    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    PaintBadge(atlas, *factory.Get(), artwork::kArrangeMedium, medium);
    PaintBadge(atlas, *factory.Get(), artwork::kArrangeLarge, large);
}

void ApplyKetsuiArrangeArtwork(std::span<std::uint8_t> motion) {
    const auto hash = PreparationHash(motion);
    constexpr std::string_view base = "177ec25d0257bdbd15e1513baeaf49113f19934551fde5c8f13a4f6db1de6ab8";
    constexpr std::string_view update = "12e263ab3772106f6f4627dfd74cd12e5eab7e7a75c86a67222c6ca351dd04b3";
    if (hash != base && hash != update) throw std::runtime_error("Ketsui common UI differs from the verified atlas");
    auto atlas = motion.subspan(hash == base ? 0x284a0 : 0x28500, 4096u * 2048u * 4u);
    const BadgeRegion large = hash == base ? BadgeRegion{2312, 1089, 331, 58} : BadgeRegion{1336, 1108, 331, 58};
    constexpr BadgeRegion compact{3897, 562, 188, 44};

    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    PaintKetsuiBadge(atlas, *factory.Get(), artwork::kKetsuiArrange331x58, large, false);
    PaintKetsuiBadge(atlas, *factory.Get(), artwork::kKetsuiArrange188x44, compact, false);
}

void ApplyKetsuiArrangeCardArtwork(std::span<std::uint8_t> motion) {
    constexpr std::string_view hash = "3c7e52af38d78acc3e4abc19a4245b22a44311365cf2d2721c346f6403e10f63";
    if (PreparationHash(motion) != hash) throw std::runtime_error("Ketsui Direct Menu differs from the verified atlas");

    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    auto atlas = motion.subspan(352256, 4096u * 4096u * 4u);
    // Direct Menu mode 5 uses icon 0024 exclusively.
    PaintKetsuiBadge(atlas, *factory.Get(), artwork::kKetsuiArrange171x18, {2177, 2678, 171, 18}, true);
}

void ApplyDaioujouArrangeGadgetArtwork(std::span<std::uint8_t> motion, bool update) {
    if (PreparationHash(motion) != (update ? gadgetUpdateHash : gadgetBaseHash))
        throw std::runtime_error("DaiOuJou music gadget differs from the verified atlas");

    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    auto caption = DecodeBadge(*factory.Get(), artwork::kArrangeLarge, {0, 0, 264, 56});
    ComPtr<IWICBitmap> bitmap;
    Check(factory->CreateBitmapFromMemory(264, 56, GUID_WICPixelFormat32bppRGBA, 264 * 4,
        static_cast<UINT>(caption.size()), caption.data(), bitmap.GetAddressOf()));
    ComPtr<IWICBitmapClipper> clip;
    Check(factory->CreateBitmapClipper(clip.GetAddressOf()));
    // Reuse the authored caption's first line in the compact song gadget.
    const WICRect first_line{98, 4, 116, 24};
    Check(clip->Initialize(bitmap.Get(), &first_line));
    ComPtr<IWICBitmapScaler> scaler;
    Check(factory->CreateBitmapScaler(scaler.GetAddressOf()));
    Check(scaler->Initialize(clip.Get(), 82, 17, WICBitmapInterpolationModeFant));
    std::array<BYTE, 82 * 17 * 4> label{};
    Check(scaler->CopyPixels(nullptr, 82 * 4, static_cast<UINT>(label.size()), label.data()));

    auto atlas = motion.subspan(GadgetTextureOffset(update), 4096u * 4096u * 4u);
    const unsigned left = update ? 3137 : 2592, top = update ? 2642 : 2212;
    for (unsigned y = 0; y < 17; ++y) {
        for (unsigned x = 0; x < 170; ++x) {
            auto pixel = atlas.data() + AtlasPixel(left + x, top + y);
            std::fill_n(pixel, 4, 0);
            if (x >= 44 && x < 126)
                std::copy_n(label.data() + (y * 82 + x - 44) * 4, 4, pixel);
        }
    }
}

std::vector<std::uint8_t> ExportDeathLabelGadgetSprites(std::span<const std::uint8_t> motion, bool update) {
    using namespace death_label;
    if (PreparationHash(motion) != (update ? gadgetUpdateHash : gadgetBaseHash))
        throw std::runtime_error("Unexpected DaiOuJou Death Label Gadget atlas");
    constexpr std::array<unsigned,SpriteCount> base{
        77,122,121,120,90,542,265,707,705,
        310,312,314,316,319,733,735,737,739,742,
        87,84,93,119,94,95,96,97,98,99,
        266,267,268,269,270,271,272,273,148,150,536,152,
        826,837,839,840,841,842,843,844,845,827,763,767,768,769,770,771,772,773,774,764,
        159,558,574,570,571,
        123,124,701,697,698,656,
        672,669,670,668,
        320,321,732,707,765,766,
        702,713,141,140,145,137,138,147,146,139,128,56,108,107,92,176,
    };
    constexpr std::array<unsigned,SpriteCount> patch{
        77,122,121,120,90,567,288,866,864,
        333,335,337,339,342,892,894,896,898,901,
        87,84,93,119,94,95,96,97,98,99,
        289,290,291,292,293,294,295,296,148,150,561,152,
        986,997,999,1000,1001,1002,1003,1004,1005,987,923,927,928,929,930,931,932,933,934,924,
        159,583,600,596,597,
        123,124,860,856,857,815,
        831,828,829,827,
        343,344,891,866,925,926,
        861,872,141,140,145,137,138,147,146,139,128,56,108,107,92,178,
    };
    const auto offset=GadgetTextureOffset(update);
    // The descriptor table in this PSB v3 asset uses the v2 object encoding.
    std::vector<std::uint8_t> metadata(motion.begin(),motion.begin()+offset);
    metadata[4]=2;
    const auto document=ParseResourceIndex(metadata);
    const auto& icons=document.at("source").at("tex").at("icon");
    const auto atlas=motion.subspan(offset,4096u*4096u*4u);
    std::vector<std::uint8_t> bytes;
    const auto append=[&](std::uint32_t value) {
        for(unsigned bit=0;bit<32;bit+=8)bytes.push_back(static_cast<std::uint8_t>(value>>bit));
    };
    append(GadgetMagic);
    append(SpriteCount);
    const auto& ids = update ? patch : base;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        char key[5];
        std::snprintf(key,sizeof key,"%04u",ids[i]);
        const auto& icon=icons.at(key);
        auto x=icon.at("left").get<unsigned>(),y=icon.at("top").get<unsigned>();
        auto width=icon.at("width").get<unsigned>();
        auto height=icon.at("height").get<unsigned>();
        if (i == BossPanel) height = std::min(height,55u);
        if (i == TimeCaption) {
            x+=7;
            y+=66;
            width=46;
            height=21;
        }
        if (i == ScoreDetails) {
            // Preserve the original Total Get Point caption for the stage subtotal.
            x+=185;
            y+=55;
            width=118;
            height=21;
        }
        constexpr unsigned gridRows = 8;
        const unsigned extension = i == BattlePanel ? 128 : 0;
        const unsigned outputHeight = height + extension;
        if(!width || !height || width>4096 || height>4096-extension || x>4096-width || y>4096-height ||
           (extension && height<2*gridRows) ||
           std::uint64_t{width}*outputHeight*4+bytes.size()>GadgetPixelBudget)
            throw std::runtime_error("Invalid Death Label Gadget sprite bounds");
        append(width);
        append(outputHeight);
        const auto spriteStart = bytes.size();
        auto destination = spriteStart;
        bytes.resize(destination + std::size_t{width} * outputHeight * 4);
        // Colors from the original gauge motions, in atlas byte order.
        std::array<std::uint8_t,3> tint{};
        switch (i) {
        case BossHealthFill:
        case HyperTimerFill: tint={57,57,207}; break;
        case PowerSegment: tint={43,133,232}; break;
        case HyperTimerEmpty: tint={83,82,83}; break;
        default: break;
        }
        for (unsigned row = 0; row < outputHeight; ++row) {
            unsigned sourceRow = row;
            if (extension && row >= height-gridRows) {
                // Extend the blank grid just above the original lower border.
                // Keep the header, caption and border at their native scale.
                sourceRow = row >= outputHeight-gridRows ? row-extension :
                    height-2*gridRows+(row-(height-gridRows))%gridRows;
            }
            for (unsigned column = 0; column < width; ++column, destination += 4) {
                std::copy_n(atlas.data() + AtlasPixel(x + column,y + sourceRow),4,bytes.data() + destination);
                if (tint[2]) std::copy(tint.begin(),tint.end(),bytes.data() + destination);
            }
        }
        if (i == RoundPanel) {
            // The boss rush uses the original frame with stage and run counters.
            // Recover the grid under the baked captions from panels with the same background.
            const std::array names{update ? "0274" : "0251",update ? "0275" : "0252","0159"};
            std::array<std::array<unsigned,2>,names.size()> backgrounds{};
            for (std::size_t index = 0; index < backgrounds.size(); ++index) {
                const auto& background = icons.at(names[index]);
                backgrounds[index] = {background.at("left").get<unsigned>(),
                                      background.at("top").get<unsigned>()};
            }
            const auto brightness = [](const std::uint8_t* pixel) {
                return pixel[0] + pixel[1] + pixel[2];
            };
            const auto restoreGrid = [&](unsigned left,unsigned top,unsigned right,unsigned bottom) {
                for (unsigned row = top; row < bottom; ++row) {
                    for (unsigned column = left; column < right; ++column) {
                        auto* pixel = bytes.data() + spriteStart + (row * width + column) * 4;
                        const std::uint8_t* grid = pixel;
                        for (const auto& [bx,by] : backgrounds) {
                            const auto* candidate = atlas.data() + AtlasPixel(bx + column,by + row);
                            if (brightness(candidate) < brightness(grid)) grid = candidate;
                        }
                        if (grid != pixel) std::copy_n(grid,4,pixel);
                    }
                }
            };
            restoreGrid(92,30,242,79);
            if (!update) {
                restoreGrid(92,98,124,110);
                restoreGrid(170,103,197,113);
            }
        }
        if (i == ComboPanel && update) {
            // 01.04 moved the boss-hit caption off the panel and into its own layer.
            const auto& caption=icons.at("0594");
            const auto cx=caption.at("left").get<unsigned>(),cy=caption.at("top").get<unsigned>();
            const auto cw=caption.at("width").get<unsigned>(),ch=caption.at("height").get<unsigned>();
            auto* panel=bytes.data()+bytes.size()-std::size_t{width}*height*4;
            for (unsigned row=0; row<ch; ++row) {
                for (unsigned column=0; column<cw; ++column) {
                    const auto* ink=atlas.data()+AtlasPixel(cx+column,cy+row);
                    auto* pixel=panel+((36+row)*width+10+column)*4;
                    for (unsigned c=0; c<3; ++c)
                        pixel[c]=static_cast<std::uint8_t>((ink[c]*ink[3]+pixel[c]*(255-ink[3])+127)/255);
                }
            }
        }
    }
    return bytes;
}

void ApplyKetsuiArrangeGadgetArtwork(std::span<std::uint8_t> motion) {
    constexpr std::array<std::string_view, 2> hashes{
        "da4a128f7cbe3f15ee0ee8c6ef5d1556a08e3d709030086fb0f3ce85234f5540",
        "50780f166d1fca05c17461c9ba3bd562b02e2781495ba0da849ff198d113215e",
    };
    if (std::ranges::find(hashes, PreparationHash(motion)) == hashes.end())
        throw std::runtime_error("Ketsui music gadget differs from the verified atlas");

    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    auto atlas = motion.subspan(628928 + 1024u * 1024u * 4u, 4096u * 4096u * 4u);
    constexpr BadgeRegion region{298, 2690, 160, 13};
    const auto label = DecodeBadge(*factory.Get(), artwork::kKetsuiArrange160x13, region);
    for (unsigned y = 0; y < region.height; ++y) {
        for (unsigned x = 0; x < region.width; ++x) {
            auto* pixel = atlas.data() + AtlasPixel(region.x + x, region.y + y);
            const auto* ink = label.data() + (y * region.width + x) * 4;
            std::copy_n(ink, 4, pixel);
            std::swap(pixel[0],pixel[2]);
        }
    }
}
}
