#include <Windows.h>

#include "../graphics/texture.h"
#include "cache_memory.h"
#include "cache_tests.h"
#include "gpu_worker.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Ketsui::GpuCore {
namespace {
struct CloseMapping {
  void operator()(void *p) const {
    if (p) {
      CloseHandle(p);
    }
  }
};
struct UnmapView {
  void operator()(void *p) const {
    if (p) {
      UnmapViewOfFile(p);
    }
  }
};

class SectionMemory final : public CacheMemoryBackend {
  static constexpr u64 bytes = 2_MB;
  static constexpr u64 page = 4096;
  GpuWorker &worker;
  std::unique_ptr<void, CloseMapping> section{CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, nullptr)};
  std::unique_ptr<void, UnmapView> backing;
  std::array<std::unique_ptr<void, UnmapView>, 2> views;
  std::array<std::array<unsigned, bytes / page>, 2> protections;

public:
  std::function<void(u64, u64, bool)> fault;
  std::uint32_t reads{}, writes{};
  explicit SectionMemory(GpuWorker &worker) : worker{worker} {
    if (!section) {
      throw std::runtime_error("Cache fixture section creation failed");
    }
    backing.reset(
        MapViewOfFile(section.get(), FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    views[0].reset(MapViewOfFileEx(section.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                                   bytes,
                                   reinterpret_cast<void *>(0x2000000000ull)));
    views[1].reset(MapViewOfFileEx(section.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                                   bytes,
                                   reinterpret_cast<void *>(0x2100000000ull)));
    if (!backing || !views[0] || !views[1]) {
      throw std::runtime_error("Cache fixture shared mapping failed");
    }
    for (auto &protection : protections) {
      protection.fill(3);
    }
  }
  ~SectionMemory() { Restore(); }
  u64 Base(unsigned view = 0) const {
    return reinterpret_cast<u64>(views[view].get());
  }
  u8 *Backing(u64 offset) { return static_cast<u8 *>(backing.get()) + offset; }
  u8 *Cpu(u64 offset, unsigned view = 0) {
    return static_cast<u8 *>(views[view].get()) + offset;
  }
  bool IsMapped(u64 address, u64 size) const noexcept override {
    if (!size || size > bytes) {
      return false;
    }
    for (const auto &view : views) {
      const u64 base = reinterpret_cast<u64>(view.get());
      if (address >= base && address - base <= bytes - size) {
        return true;
      }
    }
    return false;
  }
  u64 Physical(u64 address) const {
    for (const auto &view : views) {
      const auto base = reinterpret_cast<u64>(view.get());
      if (address >= base && address - base < bytes) {
        return address - base;
      }
    }
    throw std::runtime_error("Cache fixture address has no backing");
  }
  void ReadSparse(u64 address, void *destination, u64 size) override {
    auto *out = static_cast<u8 *>(destination);
    while (size) {
      const auto amount = std::min<u64>(16_KB - address % 16_KB, size);
      if (IsMapped(address, amount)) {
        std::memcpy(out, Backing(Physical(address)), amount);
      } else {
        std::memset(out, 0, amount);
      }
      size -= amount;
      address += amount;
      out += amount;
    }
  }
  bool WriteBacking(u64 address, const void *source, u64 size) override {
    if (!IsMapped(address, size)) {
      return false;
    }
    std::memcpy(Backing(Physical(address)), source, size);
    return true;
  }
  u64 MappedBytes(u64 address, u64 maximum) const noexcept override {
    for (const auto& view : views) {
      const auto base = reinterpret_cast<u64>(view.get());
      if (address >= base && address - base < bytes) { return std::min(maximum, bytes - (address - base)); }
    }
    return 0;
  }
  void ReadMetadata(u64 address, void *destination, u64 size) override {
    if (!IsMapped(address, size) || !fault) {
      throw std::runtime_error("Invalid cache metadata read");
    }
    const auto canonical = Base() + Physical(address);
    RunGpuSync([&] { fault(canonical, size, false); });
    ReadSparse(address, destination, size);
  }
  void ReadProgram(u64 address, void* destination, u64 size) override { ReadMetadata(address, destination, size); }
  void Protect(u64 address, u64 size, unsigned permissions) override {
    if (!IsMapped(address, size) || address % page || size % page ||
        permissions > 3 || permissions == 2) {
      throw std::runtime_error("Invalid cache protection request");
    }
    unsigned view_index = address >= Base(1) ? 1 : 0;
    const auto begin = Physical(address) / page;
    for (u64 p = begin; p < begin + size / page; ++p) {
      protections[view_index][p] = permissions;
      const auto allowed = protections[0][p] & protections[1][p];
      const DWORD protection = allowed == 3   ? PAGE_READWRITE
                               : allowed == 1 ? PAGE_READONLY
                                              : PAGE_NOACCESS;
      for (const auto &view : views) {
        DWORD previous{};
        if (!VirtualProtect(static_cast<u8 *>(view.get()) + p * page, page,
                            protection, &previous)) {
          throw std::runtime_error("Cache page protection failed");
        }
      }
    }
  }
  bool HandleAccess(u64 address, u64 size, bool write, bool) override {
    if (!IsMapped(address, size) || !fault) {
      return false;
    }
    const auto canonical = Base() + Physical(address);
    RunGpuSync([&, canonical] {
      if (write) {
        ++writes;
      } else {
        ++reads;
      }
      fault(canonical, size, write);
    });
    return true;
  }
  bool IsGpuThread() const noexcept override { return worker.IsCurrent(); }
  void RunGpuSync(std::function<void()> command) override {
    worker.Sync(std::move(command));
  }
  void Restore() {
    for (const auto &view : views) {
      if (view) {
        DWORD old{};
        if (!VirtualProtect(view.get(), bytes, PAGE_READWRITE, &old)) {
          std::terminate();
        }
      }
    }
  }
};

struct CachePair {
  Vulkan::Scheduler scheduler;
  Vulkan::Runtime runtime;
  VideoCore::PageManager pages;
  VideoCore::BufferCache buffers;
  VideoCore::TextureCache textures;
  CachePair(const Vulkan::Instance &instance, SectionMemory &memory)
      : scheduler{instance}, runtime{instance, scheduler}, pages{nullptr},
        buffers{instance, scheduler, runtime, nullptr, textures, pages},
        textures{instance, scheduler, runtime, nullptr, buffers, pages} {
    scheduler.SetSessionCallback([this] { runtime.FlushBarriers(); });
    scheduler.SetSubmitCallback([this](Vulkan::SubmitInfo &info) {
      buffers.SubmitPendingArenaBinds(info);
    });
    pages.OnGpuMap(memory.Base(), 2_MB);
  }
  ~CachePair() { scheduler.Finish(); }
};

class CacheSession {
public:
  GpuWorker worker;
  SectionMemory memory{worker};
  CacheMemoryScope memory_scope{memory};
  std::unique_ptr<CachePair> caches;
  explicit CacheSession(const Vulkan::Instance &instance) {
    worker.Sync(
        [&] { caches = std::make_unique<CachePair>(instance, memory); });
    memory.fault = [&](u64 addr, u64 size, bool write) {
      if (write) {
        caches->buffers.InvalidateMemory(addr, size, true);
        caches->textures.InvalidateMemory(addr, size);
      } else {
        caches->buffers.ReadMemory(addr, size, false, true);
      }
    };
  }
  ~CacheSession() {
    worker.Sync([&] { caches.reset(); });
    memory.fault = {};
    memory.Restore();
  }
};
} // namespace

CacheResults TestCaches(const Vulkan::Instance &instance) {
  CacheResults result;
  const auto check = [&](bool ok, const char *text) {
    if (!ok) {
      throw std::runtime_error(text);
    }
    ++result.checks;
  };
  CacheSession session{instance};
  auto &memory = session.memory;
  auto &pair = *session.caches;
  constexpr u64 offset = 0x10000, bytes = 0x8000;
  auto *cpu = memory.Cpu(offset);
  auto *alias = memory.Cpu(offset, 1);
  std::fill_n(reinterpret_cast<u32 *>(cpu), bytes / 4, 0x11112222);
  const VideoCore::Buffer *first_buffer{};
  const auto download_buffer = [&] {
    std::vector<u8> snapshot(bytes);
    session.worker.Sync([&] {
      const auto [buffer, source_offset] =
          pair.buffers.ObtainBuffer(memory.Base() + offset, bytes, false);
      if (!first_buffer) {
        first_buffer = buffer;
      } else {
        check(buffer == first_buffer,
              "Cache recreated unchanged resident buffer");
      }
      const auto download = pair.runtime.GetStagingPool().Request(
          bytes, VideoCore::MemoryType::HostCached);
      const vk::BufferCopy copy{.srcOffset = source_offset,
                                .dstOffset = download.offset,
                                .size = bytes};
      pair.runtime.CopyBuffer(buffer, download.buffer, {&copy, 1});
      pair.scheduler.Finish();
      download.Invalidate();
      std::memcpy(snapshot.data(), download.mapped, bytes);
    });
    result.verified_bytes += bytes;
    return snapshot;
  };
  auto snapshot = download_buffer();
  check(std::memcmp(snapshot.data(), memory.Backing(offset), bytes) == 0,
        "Resident buffer initial upload mismatch");
  alias[11] = 0x9a;
  check(memory.writes > 0, "CPU alias write did not reach cache invalidation");
  snapshot = download_buffer();
  check(std::memcmp(snapshot.data(), memory.Backing(offset), bytes) == 0 &&
            snapshot[11] == 0x9a,
        "CPU alias write was not reuploaded");
  const auto gpu_fill = [&](u32 value) {
    session.worker.Sync([&] {
      const auto [buffer, dst] =
          pair.buffers.ObtainBuffer(memory.Base() + offset, bytes, true);
      pair.runtime.FillBuffer(buffer, dst, bytes, value);
    });
  };
  gpu_fill(0x33334444);
  MEMORY_BASIC_INFORMATION query{};
  check(VirtualQuery(alias, &query, sizeof(query)) &&
            query.Protect == PAGE_NOACCESS,
        "GPU writes did not guard CPU alias reads");
  const auto value = *reinterpret_cast<volatile u32 *>(alias);
  check(value == 0x33334444 && memory.reads > 0,
        "CPU alias read did not synchronize pending GPU data");
  gpu_fill(0x55667788);
  alias[7] = 0xab;
  check(cpu[7] == 0xab && cpu[8] == 0x88 && cpu[9] == 0x77 && cpu[10] == 0x66 &&
            cpu[11] == 0x55,
        "Partial CPU alias write discarded surrounding GPU results");

  constexpr u64 image_offset = 0x80000;
  constexpr u64 image_bytes = 128 * 64 * 4;
  std::fill_n(reinterpret_cast<u32 *>(memory.Cpu(image_offset)),
              image_bytes / 4, 0xffeeddcc);
  VideoCore::TextureCache::ImageDesc desc{};
  desc.info.pixel_format = vk::Format::eR8G8B8A8Unorm;
  desc.info.type = AmdGpu::ImageType::Color2D;
  desc.info.size = {128, 64, 1};
  desc.info.pitch = 128;
  desc.info.num_bits = 32;
  desc.info.num_samples = 1;
  desc.info.tile_mode = AmdGpu::TileMode::DisplayLinearAligned;
  desc.info.array_mode = AmdGpu::GetArrayMode(desc.info.tile_mode);
  desc.info.guest_address = memory.Base() + image_offset;
  desc.info.UpdateSize();
  desc.type = VideoCore::TextureCache::BindingType::Texture;
  VideoCore::ImageId image_id{};
  vk::Image image_handle{};
  const auto download_texture = [&] {
    std::vector<u8> pixels(image_bytes);
    session.worker.Sync([&] {
      const auto id = pair.textures.FindImage(desc);
      (void)pair.textures.FindTexture(id, desc);
      auto &image = pair.textures.GetImage(id);
      if (image_id) {
        check(id == image_id && image.GetImage() == image_handle,
              "Texture cache recreated unchanged image");
      }
      image_id = id;
      image_handle = image.GetImage();
      const auto download = pair.runtime.GetStagingPool().Request(
          image_bytes, VideoCore::MemoryType::HostCached);
      const vk::BufferImageCopy copy{
          .bufferOffset = download.offset,
          .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                               .layerCount = 1},
          .imageExtent = {128, 64, 1}};
      pair.runtime.DownloadImage(&image, download.buffer, {&copy, 1});
      pair.scheduler.Finish();
      download.Invalidate();
      std::memcpy(pixels.data(), download.mapped, image_bytes);
    });
    result.verified_bytes += image_bytes;
    return pixels;
  };
  auto pixels = download_texture();
  check(std::memcmp(pixels.data(), memory.Backing(image_offset), image_bytes) ==
            0,
        "Texture cache initial pixel upload mismatch");
  memory.Cpu(image_offset, 1)[image_bytes - 1] = 0x42;
  pixels = download_texture();
  check(std::memcmp(pixels.data(), memory.Backing(image_offset), image_bytes) ==
                0 &&
            pixels.back() == 0x42,
        "Texture cache missed CPU write through an alias");
  for (unsigned tile : {13u, 10u, 14u}) {
    const u64 tiled_offset = 0xa0000 + (tile == 13   ? 0
                                        : tile == 10 ? 0x40000
                                                     : 0x80000);
    kt::gfx::TextureLayout layout;
    layout.address = memory.Base() + tiled_offset;
    layout.width = layout.pitch = 128;
    layout.height = layout.padded_height = 64;
    layout.bytes = image_bytes;
    layout.tiling = tile;
    std::vector<u8> expected(image_bytes);
    for (u32 y = 0; y < 64; ++y) {
      for (u32 x = 0; x < 128; ++x) {
        const auto i = (y * 128 + x) * 4;
        expected[i] = static_cast<u8>(x * 3 + y);
        expected[i + 1] = static_cast<u8>(y * 5 + x);
        expected[i + 2] = static_cast<u8>(x ^ y ^ tile);
        expected[i + 3] = static_cast<u8>(x + y * 7);
      }
    }
    kt::gfx::WriteTexturePixels(layout, expected,
                                {memory.Cpu(tiled_offset), image_bytes});
    desc.info = {};
    desc.info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    desc.info.type = AmdGpu::ImageType::Color2D;
    desc.info.size = {128, 64, 1};
    desc.info.pitch = 128;
    desc.info.num_bits = 32;
    desc.info.num_samples = 1;
    desc.info.tile_mode = static_cast<AmdGpu::TileMode>(tile);
    desc.info.array_mode = AmdGpu::GetArrayMode(desc.info.tile_mode);
    desc.info.props.is_tiled = true;
    desc.info.guest_address = layout.address;
    desc.info.UpdateSize();
    check(desc.info.guest_size == image_bytes,
          "CPU and GPU tile backing extents differ");
    image_id = {};
    image_handle = nullptr;
    pixels = download_texture();
    check(pixels == expected, "GPU texture detile differs from CPU reference");
    const auto last_alpha = kt::gfx::TextureByteOffset(layout, 127, 63) + 3;
    memory.Cpu(tiled_offset, 1)[last_alpha] ^= 0x5a;
    expected.back() ^= 0x5a;
    pixels = download_texture();
    check(pixels == expected, "Tiled cache missed CPU alias modification");
  }
  session.worker.Sync([&] { pair.scheduler.Finish(); });
  result.read_faults = memory.reads;
  result.write_faults = memory.writes;
  return result;
}
} // namespace Ketsui::GpuCore
