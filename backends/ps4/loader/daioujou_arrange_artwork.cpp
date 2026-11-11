#include "daioujou_arrange.h"
#include "daioujou_arrange_artwork_data.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Microsoft::WRL::ComPtr;

void Check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Cannot decode the X Arrange badge");
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
    if (width != badge.width || height != badge.height) throw std::runtime_error("Unexpected X Arrange badge dimensions");
    ComPtr<IWICFormatConverter> converter;
    Check(factory.CreateFormatConverter(converter.GetAddressOf()));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<std::uint8_t> rgba(width * height * 4);
    Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()));
    return rgba;
}

// The common UI atlas is 4096x2048 RGBA, stored as 8x8 Morton-ordered tiles.
std::size_t AtlasPixel(unsigned x, unsigned y) {
    unsigned inside{};
    for (unsigned i = 0; i < 3; ++i) {
        inside |= ((x >> i) & 1) << (i * 2);
        inside |= ((y >> i) & 1) << (i * 2 + 1);
    }
    return ((y / 8 * 512 + x / 8) * 64 + inside) * 4;
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

void ApplyDaioujouArrangeGadgetArtwork(std::span<std::uint8_t> motion, bool update) {
    constexpr std::string_view base_hash = "829d61051eea451a90a8998689e16b2a7c0c2a85d4530fd0dda93b6eaacbc258";
    constexpr std::string_view update_hash = "f522e1c2f11e290efbff3492c3b68e7928c9dedc5b3848989cc2851a73625bce";
    if (PreparationHash(motion) != (update ? update_hash : base_hash))
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

    auto atlas = motion.subspan(update ? 0x9e860 : 0x92fc0, 4096u * 4096u * 4u);
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
}
