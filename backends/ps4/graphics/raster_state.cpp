#include "raster.h"
#include <stdexcept>

namespace kt::gfx {
namespace {
BlendFactor Factor(unsigned value) {
    if (value <= 10) { return static_cast<BlendFactor>(value); }
    switch (value) {
    case 13: return BlendFactor::ConstantColor;
    case 14: return BlendFactor::InverseConstantColor;
    case 19: return BlendFactor::ConstantAlpha;
    case 20: return BlendFactor::InverseConstantAlpha;
    default: throw std::runtime_error("Reserved or dual-source GCN blend factor is not supported");
    }
}
BlendOperation Operation(unsigned value) {
    if (value > 4) { throw std::runtime_error("Reserved GCN blend operation"); }
    return static_cast<BlendOperation>(value);
}
}
RasterBlend DecodeGcnBlend(std::uint32_t control) {
    // CB_BLEND0_CONTROL: factors at 0/8/16/24, operations at 5/21;
    // SEPARATE_ALPHA_BLEND bit 29, ENABLE bit 30, DISABLE_ROP3 bit 31.
    // Logic-op override and reserved fields require a separate contract.
    if (control & 0x8000e000u) { throw std::runtime_error("GCN blend ROP override/reserved bits are not supported"); }
    RasterBlend result;
    result.enabled = (control & 0x40000000u) != 0;
    if (!result.enabled) { return result; }
    result.source_color = Factor(control & 31); result.destination_color = Factor((control >> 8) & 31);
    result.color = Operation((control >> 5) & 7);
    if (control & 0x20000000u) {
        result.source_alpha = Factor((control >> 16) & 31); result.destination_alpha = Factor((control >> 24) & 31);
        result.alpha = Operation((control >> 21) & 7);
    } else {
        result.source_alpha = result.source_color; result.destination_alpha = result.destination_color; result.alpha = result.color;
    }
    return result;
}
RasterStencil DecodeGcnStencil(std::uint32_t operations, std::uint32_t reference_mask, std::uint32_t compare) {
    if ((operations & ~0xfffu) || compare > 7) { throw std::runtime_error("Invalid stencil state"); }
    RasterStencil result; result.reference = reference_mask & 255; result.compare_mask = (reference_mask >> 8) & 255;
    result.write_mask = (reference_mask >> 16) & 255; result.compare = compare;
    const auto operation = [&](unsigned value) {
        switch (value) {
        case 0: return 0u; // KEEP
        case 1: return 1u; // ZERO
        case 3: return 2u; // REPLACE with test reference
        case 4:
            if ((reference_mask >> 24) == result.reference) { return 2u; }
            break; // Separate test/op reference needs shader or another path.
        case 5: return 3u; // INCREMENT_CLAMP
        case 6: return 4u; // DECREMENT_CLAMP
        case 7: return 5u; // INVERT
        case 8: return 6u; // INCREMENT_WRAP
        case 9: return 7u; // DECREMENT_WRAP
        }
        throw std::runtime_error("Stencil arithmetic or independent replace reference execution pending");
    };
    result.fail = operation(operations & 15); result.pass = operation((operations >> 4) & 15); result.depth_fail = operation((operations >> 8) & 15);
    return result;
}
}
