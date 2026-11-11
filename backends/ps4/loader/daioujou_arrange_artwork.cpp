#include "daioujou_arrange.h"
#include "daioujou_arrange_artwork_data.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
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
    for (unsigned y = 0; y < badge.height; ++y) {
        for (unsigned x = 0; x < badge.width; ++x) {
            const auto color = rgba.data() + (y * badge.width + x) * 4;
            std::copy_n(color, 4, atlas.data() + AtlasPixel(badge.x + x, badge.y + y));
        }
    }
}
}

void ApplyDaioujouArrangeArtwork(std::span<std::uint8_t> motion, bool update) {
    constexpr std::string_view base_hash = "fedf6e8aa14f4168afb74647a565e39605d81cedf71923ec1b866f971ffc8dad";
    constexpr std::string_view update_hash = "76af81f9b714fdf3eabada82431f4ace74ba90cf89a3d3f8d59415af54980587";
    if (PreparationHash(motion) != (update ? update_hash : base_hash))
        throw std::runtime_error("DaiOuJou common UI differs from the verified atlas");

    // Replace the two unused BGM badge regions. Preserve every other sprite
    // and all animation and chunk metadata in the verified PSB.
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
}
