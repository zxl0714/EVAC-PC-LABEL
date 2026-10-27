#include <iostream>
#include <rex/graphics/d3d12/native_resolve.h>
#include <stdexcept>
#include <vector>
#include <algorithm>

using namespace rex::graphics;
using namespace rex::graphics::d3d12;

int main() {
  unsigned checks = 0;
  auto check = [&](bool ok) {
    ++checks;
    if (!ok)
      throw std::runtime_error("Native resolve policy check " +
                               std::to_string(checks));
  };
  draw_util::ResolveInfo original{};
  original.color_edram_info.format =
      uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8);
  original.color_edram_info.msaa_samples = xenos::MsaaSamples::k1X;
  original.copy_dest_info.copy_dest_format = xenos::ColorFormat::k_8_8_8_8;
  check(NativeRgba8ResolveFormatSupported(original, false));
  check(!NativeRgba8ResolveFormatSupported(original, true));
  auto reject = [&](auto modify) {
    auto changed = original;
    modify(changed);
    check(!NativeRgba8ResolveFormatSupported(changed, false));
  };
  reject([](auto &i) {
    i.rb_copy_control.copy_src_select = xenos::kMaxColorRenderTargets;
  });
  reject([](auto &i) {
    i.color_edram_info.msaa_samples = xenos::MsaaSamples::k2X;
  });
  reject([](auto &i) {
    i.color_edram_info.msaa_samples = xenos::MsaaSamples::k4X;
  });
  reject([](auto &i) {
    i.color_edram_info.format =
        uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA);
  });
  reject([](auto &i) {
    i.copy_dest_info.copy_dest_format = xenos::ColorFormat::k_16_16;
  });
  reject([](auto &i) { i.copy_dest_info.copy_dest_exp_bias = 1; });
  reject([](auto &i) { i.copy_dest_info.copy_dest_exp_bias = -1; });
  reject([](auto &i) { i.copy_dest_info.copy_dest_array = 1; });
  for (unsigned endian = 0; endian < 8; ++endian) {
    auto changed = original;
    changed.copy_dest_info.copy_dest_endian = xenos::Endian128(endian);
    check(NativeRgba8ResolveFormatSupported(changed, false) == (endian < 4));
  }
  for (unsigned sample = 1; sample < 8; ++sample) {
    reject([=](auto &i) {
      i.copy_dest_coordinate_info.copy_sample_select =
          xenos::CopySampleSelect(sample);
    });
  }
  auto swapped = original;
  swapped.copy_dest_info.copy_dest_swap = 1;
  check(NativeRgba8ResolveFormatSupported(swapped, false));
  check(NativeRgba8ResolveSpanSupported(0, 16, 45, 16));
  check(NativeRgba8ResolveSpanSupported(2047, 1, 1, 1));
  check(!NativeRgba8ResolveSpanSupported(2047, 1, 2, 1));
  check(!NativeRgba8ResolveSpanSupported(0, 1, 1, 0));
  check(!NativeRgba8ResolveSpanSupported(0, 1, 0, 1));
  check(!NativeRgba8ResolveSpanSupported(0, 0, 1, 1));
  check(!NativeRgba8ResolveSpanSupported(0, 2, 1, 1));
  check(!NativeRgba8ResolveSpanSupported(UINT32_MAX, 1, 1, 1));
  check(!NativeRgba8ResolveSpanSupported(0, 1, UINT32_MAX, UINT32_MAX));
  auto full = original;
  full.coordinate_info.width_div_8 = 80;
  full.height_div_8 = 60;
  full.copy_dest_coordinate_info.pitch_aligned_div_32 = 20;
  full.copy_dest_base = full.copy_dest_extent_start = 0x100000;
  full.copy_dest_extent_length = 640 * 480 * 4;
  check(ResolveOverwritesWholePages(full, false));
  check(!ResolveOverwritesWholePages(full, true));
  auto reject_full = [&](auto modify) {
    auto changed = full;
    modify(changed);
    check(!ResolveOverwritesWholePages(changed, false));
  };
  reject_full([](auto& i) { i.coordinate_info.width_div_8 = 79; });
  reject_full([](auto& i) { i.height_div_8 = 59; });
  reject_full([](auto& i) { i.copy_dest_coordinate_info.pitch_aligned_div_32 = 21; });
  reject_full([](auto& i) { i.copy_dest_coordinate_info.offset_x_div_8 = 1; });
  reject_full([](auto& i) { i.copy_dest_coordinate_info.offset_y_div_8 = 1; });
  reject_full([](auto& i) { ++i.copy_dest_extent_start; });
  reject_full([](auto& i) { ++i.copy_dest_extent_length; });
  reject_full([](auto& i) { i.copy_dest_info.copy_dest_array = 1; });
  reject_full([](auto& i) { i.color_edram_info.msaa_samples = xenos::MsaaSamples::k2X; });
  auto partial = full;
  partial.coordinate_info.width_div_8 = 160;
  partial.height_div_8 = 90;
  partial.copy_dest_coordinate_info.pitch_aligned_div_32 = 40;
  partial.copy_dest_extent_length = 1280 * 736 * 4;
  check(!ResolveOverwritesWholePages(partial, false));
  check(ResolveWholePagePrefixLength(partial, false) == 1280 * 704 * 4);
  // Exhaustively verify the production shader's tile-address permutation: no
  // holes, overlaps or out-of-range writes in every accepted rectangle tested.
  for (uint32_t width : {32u, 64u, 96u, 640u, 1280u}) {
    for (uint32_t height : {32u, 64u, 480u, 736u}) {
      auto info = full;
      info.coordinate_info.width_div_8 = width / 8;
      info.height_div_8 = height / 8;
      info.copy_dest_coordinate_info.pitch_aligned_div_32 = width / 32;
      info.copy_dest_extent_length = width * height * 4;
      check(ResolveOverwritesWholePages(info, false));
      std::vector<uint8_t> seen(width * height);
      bool valid = true;
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          uint32_t macro = ((x >> 5) + (y >> 5) * (width >> 5)) << 9;
          uint32_t micro = ((x & 7) + ((y & 14) << 2)) << 2;
          uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 1) << 4);
          uint32_t address = ((offset & ~511u) << 3) + ((y & 16) << 7) +
              ((offset & 448) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
          if ((address & 3) || address / 4 >= seen.size() || seen[address / 4]) valid = false;
          else seen[address / 4] = 1;
          // The final partial tile row lies entirely after the full-row prefix.
          if (y >= height - 32 && address < width * (height - 32) * 4) valid = false;
        }
      }
      check(valid && std::all_of(seen.begin(), seen.end(), [](uint8_t v) { return v == 1; }));
      // Removing the final eight pixel rows preserves the preceding whole tile
      // rows. Every omitted pixel must be in the uploaded tail, not the prefix.
      info.height_div_8 = (height - 8) / 8;
      uint32_t prefix = ResolveWholePagePrefixLength(info, false);
      check(prefix == width * (height - 32) * 4);
    }
  }
  std::cout << "Native resolve policy checks=" << checks << " passed\n";
}
