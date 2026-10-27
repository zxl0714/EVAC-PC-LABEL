// Single-sample RGBA8 host target -> guest tiled shared memory.
// The caller proves complete ownership, matching format/pitch, no scaling,
// no array slice, no exponent conversion and no EDRAM wrap before dispatch.
cbuffer ResolveConstants : register(b0) {
  uint2 source_origin;
  uint2 extent;
  uint2 destination_origin;
  uint destination_pitch;
  uint destination_base;
  uint destination_endian;
  uint swap_red_blue;
};
Texture2D<float4> source_image : register(t0);
RWByteAddressBuffer destination : register(u0);

// Same 32-bit 2D tiling as texture_util::GetTiledOffset2D.
uint TiledOffset32(uint x, uint y, uint pitch) {
  uint macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << 9;
  uint micro = ((x & 7) + ((y & 14) << 2)) << 2;
  uint offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4);
  return ((offset & ~511) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
      (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
}

[numthreads(8, 8, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
  uint2 local = uint2(thread_id.x * 4, thread_id.y);
  if (any(local >= extent)) return;
  // Four neighboring pixels are contiguous in a guest 16-byte tile block.
  // Resolve destination X is aligned to eight pixels by the production caller.
  uint4 values = 0;
  [unroll] for (uint i = 0; i < 4; ++i) {
    if (local.x + i < extent.x) {
      // Match the existing host-RT dump's UNORM8 conversion, including alpha.
      uint4 rgba = uint4(source_image.Load(int3(source_origin + local + uint2(i, 0), 0)) * 255.0 + 0.5);
      if (swap_red_blue) rgba = rgba.bgra;
      uint value = rgba.r | (rgba.g << 8) | (rgba.b << 16) | (rgba.a << 24);
      if (destination_endian == 1 || destination_endian == 2)
        value = ((value & 0x00FF00FF) << 8) | ((value & 0xFF00FF00) >> 8);
      if (destination_endian == 2 || destination_endian == 3)
        value = (value << 16) | (value >> 16);
      values[i] = value;
    }
  }
  uint2 pixel = destination_origin + local;
  uint address = destination_base + TiledOffset32(pixel.x, pixel.y, destination_pitch);
  if (local.x + 4 <= extent.x) destination.Store4(address, values);
  else {
    [unroll] for (uint i = 0; i < 4; ++i)
      if (local.x + i < extent.x) destination.Store(address + i * 4, values[i]);
  }
}
