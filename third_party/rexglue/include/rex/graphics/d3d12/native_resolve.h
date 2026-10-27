// Eligibility checks shared by the single-pass copy and GPU-free regression.
#pragma once

#include <cstdint>
#include <rex/graphics/util/draw.h>

namespace rex::graphics::d3d12 {

inline bool NativeRgba8ResolveFormatSupported(const draw_util::ResolveInfo& info, bool scaled) {
  return !scaled && !info.IsCopyingDepth() &&
         info.color_edram_info.msaa_samples == xenos::MsaaSamples::k1X &&
         info.color_edram_info.format == uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) &&
         info.copy_dest_info.copy_dest_format == xenos::ColorFormat::k_8_8_8_8 &&
         !info.copy_dest_info.copy_dest_exp_bias && !info.copy_dest_info.copy_dest_array &&
         uint32_t(info.copy_dest_info.copy_dest_endian) <= 3 &&
         uint32_t(info.copy_dest_coordinate_info.copy_sample_select) == 0;
}

inline bool NativeRgba8ResolveSpanSupported(uint32_t base, uint32_t row_length, uint32_t rows,
                                            uint32_t pitch) {
  // A wrapped or split EDRAM owner needs the original transfer path. Widen the
  // arithmetic so even malformed spans cannot wrap into an apparently valid one.
  return pitch && rows && row_length && row_length <= pitch &&
         uint64_t(base) + uint64_t(rows - 1) * pitch + row_length <= xenos::kEdramTileCount;
}

inline uint32_t ResolveWholePagePrefixLength(const draw_util::ResolveInfo& info, bool scaled) {
  if (!NativeRgba8ResolveFormatSupported(info, scaled)) return 0;
  const uint32_t width = info.coordinate_info.width_div_8 * 8;
  const uint32_t height = info.height_div_8 * 8;
  // Full 32x32 RGBA8 tiles permute their 4096 bytes without holes. Exact pitch,
  // zero origin and the exact padded extent exclude offsets and row gaps.
  // Preserve the final partial tile row through the normal upload path.
  const bool valid = width && height >= 32 && !(width & 31) &&
         width == uint32_t(info.copy_dest_coordinate_info.pitch_aligned_div_32) * 32 &&
         !info.copy_dest_coordinate_info.offset_x_div_8 &&
         !info.copy_dest_coordinate_info.offset_y_div_8 &&
         info.copy_dest_base == info.copy_dest_extent_start &&
         !(info.copy_dest_extent_start & 4095) &&
         uint64_t(width) * ((height + 31) & ~31u) * 4 == info.copy_dest_extent_length;
  return valid ? width * (height & ~31u) * 4 : 0;
}

inline bool ResolveOverwritesWholePages(const draw_util::ResolveInfo& info, bool scaled) {
  const uint32_t prefix = ResolveWholePagePrefixLength(info, scaled);
  return prefix && prefix == info.copy_dest_extent_length;
}

}  // namespace rex::graphics::d3d12
