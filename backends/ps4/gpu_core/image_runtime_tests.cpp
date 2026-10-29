#include "image_runtime_tests.h"
#include "depth_transfer.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/tile_manager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace Ketsui::GpuCore {
ImageRuntimeResults TestImageRuntime(const Vulkan::Instance &instance) {
  ImageRuntimeResults results;
  const auto check = [&](bool ok, const char *text) {
    if (!ok) {
      throw std::runtime_error(text);
    }
    ++results.checks;
  };
  VmaTotalStatistics before{};
  vmaCalculateStatistics(instance.GetAllocator(), &before);
  {
    Vulkan::Scheduler scheduler{instance};
    Vulkan::Runtime runtime{instance, scheduler};
    Common::SlotVector<VideoCore::ImageView> views;
    VideoCore::ImageInfo info{};
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.type = AmdGpu::ImageType::Color2D;
    info.size = {128, 64, 1};
    info.pitch = 128;
    info.num_bits = 32;
    info.num_samples = 1;
    info.tile_mode = AmdGpu::TileMode::DisplayLinearAligned;
    info.array_mode = AmdGpu::GetArrayMode(info.tile_mode);
    info.guest_address = 0x1100000000ull;
    info.UpdateSize();
    VideoCore::Image a{instance, runtime, views, info};
    info.guest_address += 0x10000;
    VideoCore::Image b{instance, runtime, views, info};
    const auto image_a = a.GetImage(), image_b = b.GetImage();
    check(image_a && image_b && image_a != image_b,
          "Could not create resident image pair");
    constexpr u64 bytes = 128 * 64 * 4;
    auto first = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached, 256, true);
    auto second = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached, 256, true);
    vk::BufferImageCopy copy{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .layerCount = 1},
        .imageExtent = {128, 64, 1},
    };
    const auto download = [&](VideoCore::Image &image,
                              const Vulkan::StagingBufferRef &dst) {
      copy.bufferOffset = dst.offset;
      runtime.DownloadImage(&image, dst.buffer, {&copy, 1});
    };
    const auto verify = [&](const Vulkan::StagingBufferRef &ref, u32 expected) {
      ref.Invalidate();
      const auto *words = reinterpret_cast<const u32 *>(ref.mapped);
      check(std::all_of(words, words + bytes / sizeof(u32),
                        [expected](u32 v) { return v == expected; }),
            "Resident image transfer content mismatch");
      results.verified_bytes += bytes;
    };
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{1, 0, 0, 1}}});
    runtime.CopyImage(&a, &b);
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{0, 1, 0, 1}}});
    download(a, first);
    download(b, second);
    runtime.FlushBarriers();
    scheduler.Finish();
    verify(first, 0xff00ff00);
    verify(second, 0xff0000ff);
    auto upload = runtime.GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostUncached, 256);
    std::fill_n(reinterpret_cast<u32 *>(upload.mapped), bytes / sizeof(u32),
                0xffff0000);
    upload.Flush();
    copy.bufferOffset = upload.offset;
    runtime.UploadImage(&a, upload.buffer, {&copy, 1});
    runtime.CopyImage(&a, &b);
    runtime.ClearImage(
        &a, {},
        vk::ClearValue{.color = {.float32 = std::array<float, 4>{0, 0, 0, 1}}});
    download(a, first);
    download(b, second);
    runtime.FlushBarriers();
    scheduler.Finish();
    verify(first, 0xff000000);
    verify(second, 0xffff0000);
    check(a.GetImage() == image_a && b.GetImage() == image_b,
          "Image transfer recreated resident resources");
    runtime.GetStagingPool().FreeDeferred(first);
    runtime.GetStagingPool().FreeDeferred(second);
    runtime.FlushBarriers();
    scheduler.Finish();

    // Every 16-bit normalized value passes through the production GPU packing
    // shader. Nonzero suballocation offsets exercise descriptor alignment.
    constexpr u32 depth_pixels=65536;
    auto depth_in=runtime.GetStagingPool().Request(depth_pixels*2+8,VideoCore::MemoryType::HostCached,256,true);
    auto depth_24=runtime.GetStagingPool().Request(depth_pixels*4+8,VideoCore::MemoryType::HostCached,256,true);
    auto depth_out=runtime.GetStagingPool().Request(depth_pixels*2+8,VideoCore::MemoryType::HostCached,256,true);
    std::memset(depth_in.mapped,0x73,depth_in.size);
    std::memset(depth_24.mapped,0x73,depth_24.size);
    std::memset(depth_out.mapped,0x73,depth_out.size);
    auto* input16=reinterpret_cast<u16*>(depth_in.mapped+4);
    for(u32 i=0;i<depth_pixels;++i) input16[i]=u16(i);
    depth_in.Flush();depth_24.Flush();depth_out.Flush();
    Vulkan::DepthTransfer converter{instance,scheduler,runtime};
    converter.Convert(depth_in.buffer,depth_in.offset+4,depth_24.buffer,depth_24.offset+4,depth_pixels,false);
    converter.Convert(depth_24.buffer,depth_24.offset+4,depth_out.buffer,depth_out.offset+4,depth_pixels-1,true);
    runtime.FlushBarriers();scheduler.Finish();
    depth_24.Invalidate();depth_out.Invalidate();
    const auto* expanded=reinterpret_cast<const u32*>(depth_24.mapped+4);
    const auto* packed=reinterpret_cast<const u16*>(depth_out.mapped+4);
    bool expanded_equal=true,packed_equal=true;
    for(u32 i=0;i<depth_pixels;++i) expanded_equal&=expanded[i]==(u64(i)*16777215+32767)/65535;
    for(u32 i=0;i<depth_pixels-1;++i) packed_equal&=packed[i]==i;
    check(expanded_equal,"D16 GPU expansion value mismatch");
    check(packed_equal,"D24 GPU compression roundtrip mismatch");
    check(packed[depth_pixels-1]==0x7373 && *reinterpret_cast<const u32*>(depth_out.mapped)==0x73737373 &&
          *reinterpret_cast<const u32*>(depth_out.mapped+4+depth_pixels*2)==0x73737373,
          "Odd depth transfer overwrote neighboring data");
    // Exercise the image upload/download wrappers, including promoted D16S8
    // backing, instead of testing the packing shader alone.
    VideoCore::ImageInfo depth_info{};
    depth_info.pixel_format=vk::Format::eD16UnormS8Uint;
    depth_info.type=AmdGpu::ImageType::Color2D;
    depth_info.size={256,256,1};depth_info.pitch=256;
    depth_info.num_bits=16;depth_info.num_samples=1;
    depth_info.tile_mode=AmdGpu::TileMode::Depth1DThin;
    depth_info.array_mode=AmdGpu::GetArrayMode(depth_info.tile_mode);
    depth_info.props.is_depth=true;depth_info.props.has_stencil=true;
    depth_info.guest_address=0x1100200000ull;
    depth_info.UpdateSize();
    VideoCore::Image depth_image{instance,runtime,views,depth_info};
    vk::BufferImageCopy depth_copy{.bufferOffset=depth_in.offset+4,
        .imageSubresource={.aspectMask=vk::ImageAspectFlagBits::eDepth,.layerCount=1},.imageExtent={256,256,1}};
    runtime.UploadImage(&depth_image,depth_in.buffer,{&depth_copy,1});
    depth_copy.bufferOffset=depth_out.offset+4;
    runtime.DownloadImage(&depth_image,depth_out.buffer,{&depth_copy,1});
    runtime.FlushBarriers();scheduler.Finish();depth_out.Invalidate();
    packed_equal=true;for(u32 i=0;i<depth_pixels;++i) packed_equal&=packed[i]==i;
    check(packed_equal,"D16S8 actual image transfer mismatch");
    // D24 is optional. Exercise the same guest-D16 transfer against an actual
    // D32S8 backing, including all 65536 normalized values and an odd tail.
    converter.Convert(depth_in.buffer,depth_in.offset+4,depth_24.buffer,depth_24.offset+4,depth_pixels,false,true);
    converter.Convert(depth_24.buffer,depth_24.offset+4,depth_out.buffer,depth_out.offset+4,depth_pixels-1,true,true);
    runtime.FlushBarriers();scheduler.Finish();depth_24.Invalidate();depth_out.Invalidate();
    const auto* expanded_float=reinterpret_cast<const float*>(depth_24.mapped+4);
    expanded_equal=true;packed_equal=true;
    for(u32 i=0;i<depth_pixels;++i)
      expanded_equal&=std::abs(expanded_float[i]-float(i)/65535.0f)<=std::numeric_limits<float>::epsilon();
    for(u32 i=0;i<depth_pixels-1;++i) packed_equal&=packed[i]==i;
    check(expanded_equal,"D16 to D32 GPU expansion mismatch");
    check(packed_equal,"D32 GPU compression roundtrip mismatch");
    check(packed[depth_pixels-1]==65535 && *reinterpret_cast<const u32*>(depth_out.mapped)==0x73737373 &&
          *reinterpret_cast<const u32*>(depth_out.mapped+4+depth_pixels*2)==0x73737373,
          "Odd float depth transfer overwrote neighboring data");
    if(instance.IsFormatSupported(vk::Format::eD32SfloatS8Uint,
        vk::FormatFeatureFlagBits2::eDepthStencilAttachment|vk::FormatFeatureFlagBits2::eTransferSrc|
        vk::FormatFeatureFlagBits2::eTransferDst)) {
    depth_info.pixel_format=vk::Format::eD32SfloatS8Uint;
    depth_info.num_bits=32;depth_info.guest_address+=0x40000;depth_info.UpdateSize();
    VideoCore::Image depth_float_image{instance,runtime,views,depth_info};
    // Match fallback ownership: the resource retains its guest format while
    // its already-created Vulkan image has the supported float format.
    depth_float_image.info.pixel_format=vk::Format::eD16UnormS8Uint;
    depth_copy.bufferOffset=depth_in.offset+4;
    runtime.UploadImage(&depth_float_image,depth_in.buffer,{&depth_copy,1});
    depth_copy.bufferOffset=depth_out.offset+4;
    runtime.DownloadImage(&depth_float_image,depth_out.buffer,{&depth_copy,1});
    runtime.FlushBarriers();scheduler.Finish();depth_out.Invalidate();
    packed_equal=true;for(u32 i=0;i<depth_pixels;++i) packed_equal&=packed[i]==i;
    check(packed_equal,"D16S8 guest to D32S8 actual backing transfer mismatch");
    }
    results.verified_bytes+=depth_pixels*16;
    runtime.GetStagingPool().FreeDeferred(depth_in);
    runtime.GetStagingPool().FreeDeferred(depth_24);
    runtime.GetStagingPool().FreeDeferred(depth_out);
    runtime.FlushBarriers();scheduler.Finish();

    // Independent 32bpp Thin2DThin/P16 address fixture (AMD AddrLib SI pipe
    // equations), not a tiler->detiler roundtrip that could share the same bug.
    auto neo_info=info;
    neo_info.size={256,256,1};neo_info.pitch=256;
    neo_info.props.is_tiled=true;neo_info.alt_tile=true;
    neo_info.tile_mode=AmdGpu::TileMode::Thin2DThin;
    neo_info.array_mode=AmdGpu::GetArrayMode(neo_info.tile_mode);
    neo_info.UpdateSize();
    check(neo_info.guest_size==256*256*4,"Unexpected NEO macro extent");
    VideoCore::StreamBuffer parameters{instance,scheduler,VideoCore::MemoryType::Stream,0x10000};
    VideoCore::TileManager tiles{instance,scheduler,runtime,parameters};
    auto tiled=runtime.GetStagingPool().Request(neo_info.guest_size,VideoCore::MemoryType::HostCached,256,true);
    auto linear=runtime.GetStagingPool().Request(neo_info.guest_size,VideoCore::MemoryType::HostCached,256,true);
    const auto neo_offset=[](u32 x,u32 y) {
      const auto b=[](u32 v,u32 bit){return(v>>bit)&1;};
      const auto element=b(x,0)|(b(y,0)<<1)|(b(x,1)<<2)|(b(y,1)<<3)|(b(x,2)<<4)|(b(y,2)<<5);
      const auto pipe=(b(x,4)^b(y,3))|((b(x,3)^b(y,4))<<1)|((b(x,5)^b(y,6))<<2)|((b(x,6)^b(y,5))<<3);
      const auto tx=x/128,ty=y/8;
      const auto bank=(b(tx,0)^b(ty,3))|((b(tx,1)^b(ty,2)^b(ty,3))<<1)|
          ((b(tx,2)^b(ty,1))<<2)|((b(tx,3)^b(ty,0))<<3);
      return element*4|(pipe<<8)|(bank<<12)|(((y/128)*2+x/128)<<16);
    };
    for(u32 y=0;y<256;++y)for(u32 x=0;x<256;++x) {
      const u32 value=0xff000000u|(y<<8)|x;
      std::memcpy(tiled.mapped+neo_offset(x,y),&value,4);
    }
    tiled.Flush();
    const auto [detiled,detiled_offset]=tiles.DetileImage(tiled.buffer,tiled.offset,neo_info);
    VideoCore::Image neo_image{instance,runtime,views,neo_info};
    vk::BufferImageCopy neo_copy{.bufferOffset=detiled_offset,.bufferRowLength=256,.bufferImageHeight=256,
        .imageSubresource={.aspectMask=vk::ImageAspectFlagBits::eColor,.layerCount=1},.imageExtent={256,256,1}};
    runtime.UploadImage(&neo_image,detiled,{&neo_copy,1});
    neo_copy.bufferOffset=linear.offset;runtime.DownloadImage(&neo_image,linear.buffer,{&neo_copy,1});
    runtime.FlushBarriers();scheduler.Finish();linear.Invalidate();
    bool neo_equal=true;
    const auto* pixels=reinterpret_cast<const u32*>(linear.mapped);
    for(u32 i=0;i<65536;++i)neo_equal&=pixels[i]==(0xff000000u|i);
    check(neo_equal,"NEO detiler disagrees with independent P16 address fixture");
    for(u32 i=0;i<65536;++i)reinterpret_cast<u32*>(linear.mapped)[i]=0xff000000u|(65535-i);
    linear.Flush();neo_copy.bufferOffset=linear.offset;
    runtime.UploadImage(&neo_image,linear.buffer,{&neo_copy,1});
    neo_copy.bufferOffset=0;
    tiles.TileImage(neo_image,{&neo_copy,1},tiled.buffer,tiled.offset);
    runtime.FlushBarriers();scheduler.Finish();tiled.Invalidate();
    neo_equal=true;
    for(u32 y=0;y<256;++y)for(u32 x=0;x<256;++x) {
      u32 value{};std::memcpy(&value,tiled.mapped+neo_offset(x,y),4);
      neo_equal&=value==(0xff000000u|(65535-y*256-x));
    }
    check(neo_equal,"NEO tiler disagrees with independent P16 address fixture");
    results.verified_bytes+=neo_info.guest_size*2;
    runtime.GetStagingPool().FreeDeferred(tiled);runtime.GetStagingPool().FreeDeferred(linear);
    runtime.FlushBarriers();scheduler.Finish();
  }
  VmaTotalStatistics after{};
  vmaCalculateStatistics(instance.GetAllocator(), &after);
  check(after.total.statistics.allocationCount ==
            before.total.statistics.allocationCount,
        "Image runtime leaked Vulkan allocations");
  return results;
}
} // namespace Ketsui::GpuCore
