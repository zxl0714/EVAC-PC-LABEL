#include "runtime_api.h"
#include "gpu_metrics.h"
#include "gpu_program.h"
#include "presenter.h"
#include "sdl_window.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/texture_cache/image.h"
#include <Windows.h>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <stdexcept>

namespace Ketsui::GpuCore {
namespace {
void Require(bool value, const char *text) {
  if (!value) {
    throw std::runtime_error(text);
  }
}
struct Session {
  CacheMemoryScope memory_scope;
  ProgramStore programs;
  Frontend::WindowSDL window;
  std::unique_ptr<Vulkan::Instance> instance;
  std::unique_ptr<Vulkan::Scheduler> scheduler;
  std::unique_ptr<Vulkan::Runtime> runtime;
  AmdGpu::Liverpool command;
  std::unique_ptr<Vulkan::Rasterizer> rasterizer;
  std::unique_ptr<Presenter> presenter;
  bool vsync = true;
  Session(const HostSettings &settings, CacheMemoryBackend &memory)
      : memory_scope{memory},
        window{Frontend::WindowSystemInfo{
            .render_surface = settings.native_window,
            .type = (settings.native_window || settings.presentation_support)
                        ? Frontend::WindowSystemType::Windows
                        : Frontend::WindowSystemType::Headless}} {
    vsync = settings.vsync;
    ConfigureHost(settings);
    instance = std::make_unique<Vulkan::Instance>(window, -1,
                                                  settings.validation_core);
    scheduler = std::make_unique<Vulkan::Scheduler>(*instance);
    runtime = std::make_unique<Vulkan::Runtime>(*instance, *scheduler);
    ProgramScope program_scope{programs};
    rasterizer = std::make_unique<Vulkan::Rasterizer>(*instance, *scheduler,
                                                      *runtime, &command);
    if (settings.native_window) {
      presenter =
          std::make_unique<Presenter>(*instance, window, *scheduler, *runtime,
                                      rasterizer->GetTextureCache(), vsync);
    }
  }
  void Drain() {
    if (presenter) {
      presenter->Drain();
    }
    rasterizer->OnFence();
    rasterizer->Finish();
    scheduler->PopPendingOperations();
    auto complete = std::make_shared<std::promise<void>>();
    auto done = complete->get_future();
    // The first Finish has submitted and waited for all recorded GPU work.
    // Retire prior CPU download callbacks on that submitted timeline tick;
    // assigning this FIFO barrier to the next tick forced an empty submit and
    // a second device wait on every ACQUIRE_MEM. Both completion obligations
    // remain, without creating new GPU work solely to wake the callback queue.
    scheduler->DeferSubmittedPriorityOperation([complete] { complete->set_value(); });
    Require(done.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "GPU completion callbacks did not drain");
    done.get();
    scheduler->PopPendingOperations();
  }
  ~Session() {
    presenter.reset();
    rasterizer.reset();
    scheduler->SetSubmitCallback({});
  }
};
Session &Get(void *context) {
  Require(context != nullptr, "GPU runtime context is null");
  auto &session = *static_cast<Session *>(context);
  Require(session.command.owner == std::this_thread::get_id(),
          "GPU runtime called outside its owning worker");
  return session;
}
template <typename Function>
bool Guard(RuntimeError *output, Function operation) noexcept {
  if (output) {
    output->message[0] = 0;
  }
  try {
    operation();
    return true;
  } catch (const std::exception &error) {
    if (output) {
      strncpy_s(output->message, error.what(), _TRUNCATE);
    }
  } catch (...) {
    if (output) {
      strcpy_s(output->message, "Unknown GPU runtime error");
    }
  }
  return false;
}
bool Create(const HostSettings *settings, CacheMemoryBackend *memory,
            void **output, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    Require(settings && memory && output,
            "Missing GPU runtime creation parameter");
    auto session = std::make_unique<Session>(*settings, *memory);
    *output = session.release();
  });
}
bool Destroy(void *context, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    session.Drain();
    delete &session;
  });
}
bool Map(void *context, std::uint64_t address, std::uint64_t bytes,
         RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(bytes && CacheMemory().IsMapped(address, bytes),
            "GPU map has no native backing");
    session.rasterizer->GetBufferCache().InvalidateMemory(address, bytes, true);
    session.rasterizer->GetTextureCache().UnmapMemory(address, bytes);
    session.rasterizer->MapMemory(address, bytes);
    session.rasterizer->RegisterMemory(address, bytes);
  });
}
bool Access(void *context, std::uint64_t address, std::uint64_t bytes,
            bool write, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(bytes, "Empty GPU memory access");
    if (write) {
      session.rasterizer->OnFence();
      session.rasterizer->InvalidateMemory(address, bytes, true);
    } else {
      session.rasterizer->ReadMemory(address, bytes, true);
    }
  });
}
bool Unmap(void *context, std::uint64_t address, std::uint64_t bytes,
           RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    session.rasterizer->OnFence();
    session.rasterizer->UnmapMemory(address, bytes);
    session.rasterizer->Finish();
  });
}
bool Drain(void *context, RuntimeError *error) noexcept {
  return Guard(error, [&] { Get(context).Drain(); });
}
bool Registers(void *context, std::uint32_t offset, const std::uint32_t *data,
               std::uint32_t count, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(data && count && offset <= AmdGpu::Regs::NumRegs &&
                count <= AmdGpu::Regs::NumRegs - offset,
            "GPU register update exceeds register bank");
    std::memcpy(reinterpret_cast<u32 *>(&session.command.regs) + offset, data,
                std::size_t{count} * 4);
  });
}
bool Extent(void *context, std::uint32_t target, std::uint32_t width,
            std::uint32_t height, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(target <= AmdGpu::NUM_COLOR_BUFFERS && ((!width && !height) || (width && height)) &&
                width <= UINT16_MAX && height <= UINT16_MAX,
            "Invalid color target extent");
    auto& extent = target == AmdGpu::NUM_COLOR_BUFFERS ? session.command.last_db_extent : session.command.last_cb_extent[target];
    extent.width = static_cast<u16>(width);
    extent.height = static_cast<u16>(height);
  });
}
bool Draw(void *context, bool indexed, std::uint32_t offset,
          RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    ProgramScope scope{session.programs};
    session.rasterizer->Draw(indexed, offset);
  });
}
bool Flush(void *context, std::uint64_t *tick, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(tick, "Missing GPU completion tick output");
    *tick = session.rasterizer->Flush();
  });
}
bool ReadColor(void *context, std::uint64_t address, std::uint32_t width,
               std::uint32_t height, void *output, std::uint64_t bytes,
               RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(output && width && height &&
                u64{width} * height <= UINT64_MAX / 4 &&
                bytes == u64{width} * height * 4,
            "Invalid diagnostic RGBA readback extent");
    auto &cache = session.rasterizer->GetTextureCache();
    const auto id = cache.FindImageFromRange(address, bytes, false);
    Require(bool(id), "No resident GPU color image for diagnostic readback");
    auto &image = cache.GetImage(id);
    Require(image.info.guest_address == address &&
                image.info.size.width >= width &&
                image.info.size.height >= height && image.info.num_bits == 32 &&
                (image.info.pixel_format == vk::Format::eR8G8B8A8Unorm ||
                 image.info.pixel_format == vk::Format::eR8G8B8A8Srgb) &&
                image.info.num_samples == 1 &&
                image.aspect_mask == vk::ImageAspectFlagBits::eColor,
            "Diagnostic image format/extent is not RGBA32");
    const auto download = session.runtime->GetStagingPool().Request(
        bytes, VideoCore::MemoryType::HostCached);
    const vk::BufferImageCopy copy{
        .bufferOffset = download.offset,
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageExtent = {width, height, 1}};
    session.runtime->DownloadImage(&image, download.buffer, {&copy, 1});
    session.rasterizer->Finish();
    download.Invalidate();
    std::memcpy(output, download.mapped, bytes);
  });
}
RuntimeStats Stats() noexcept {
  return {DrawCount(), DispatchCount(), ValidationErrors()};
}
bool Present(void *context, const RuntimeDisplay *display,
             RuntimePresentResult *result, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(display && result && session.presenter,
            "Runtime has no presentation window or request");
    *result = session.presenter->Present(*display);
    if (result->status == RuntimePresentStatus::Completed) {
      session.rasterizer->OnSubmit();
    }
  });
}
RuntimeRegisterLayout Layout() noexcept {
#define REG_WORD(member)                                                       \
  static_cast<std::uint32_t>(offsetof(AmdGpu::Regs, member) / sizeof(u32))
  return {AmdGpu::Regs::NumRegs,       REG_WORD(vs_program),
          REG_WORD(ps_program),        REG_WORD(color_buffers),
          REG_WORD(color_target_mask), REG_WORD(color_shader_mask),
          REG_WORD(color_control),     REG_WORD(color_export_format),
          REG_WORD(shader_pos_format), REG_WORD(vs_output_control),
          REG_WORD(stage_enable),      REG_WORD(primitive_type),
          REG_WORD(num_indices),       REG_WORD(num_instances),
          REG_WORD(viewports),         REG_WORD(viewport_control),
          REG_WORD(screen_scissor),    REG_WORD(window_scissor),
          REG_WORD(generic_scissor),   REG_WORD(viewport_depths)};
#undef REG_WORD
}
bool SetWindow(void *context, void *window, RuntimeError *error) noexcept {
  return Guard(error, [&] {
    auto &session = Get(context);
    Require(
        !window || (IsWindow(static_cast<HWND>(window)) &&
                    session.instance->HasPresentFences()),
        "Window binding requires a live HWND and presentation-enabled device");
    session.Drain();
    session.presenter.reset();
    session.window = Frontend::WindowSDL{Frontend::WindowSystemInfo{
        .render_surface = window, .type = Frontend::WindowSystemType::Windows}};
    if (window) {
      session.presenter = std::make_unique<Presenter>(
          *session.instance, session.window, *session.scheduler,
          *session.runtime, session.rasterizer->GetTextureCache(), session.vsync);
    }
    std::fprintf(stderr, "gpu_runtime_window: bound=%s\n",
                 window ? "true" : "false");
  });
}
bool Clear(void* context, RuntimeError* error) noexcept {
  return Guard(error, [&] {
    auto& session = Get(context);
    session.command.regs.SetDefaults();
    // CB_TARGET_MASK's hardware CLEAR_STATE value (Linux clearstate_ci.h).
    session.command.regs.reg_array[AmdGpu::Regs::ContextRegWordOffset + 0x8e] = 0xffffffffu;
    session.command.last_cb_extent = {};
    session.command.last_db_extent = {};
  });
}
} // namespace
} // namespace Ketsui::GpuCore
extern "C" __declspec(dllexport) bool
KetsuiGpuRuntimeGetApi(std::uint32_t version, std::uint32_t bytes,
                       Ketsui::GpuCore::RuntimeApi *output) noexcept {
  using namespace Ketsui::GpuCore;
  if (!output || version != RuntimeApiVersion || bytes != sizeof(RuntimeApi)) {
    return false;
  }
  *output = {RuntimeApiVersion,
             sizeof(RuntimeApi),
             Create,
             Destroy,
             Map,
             Access,
             Unmap,
             Drain,
             Registers,
             Extent,
             Draw,
             Flush,
             ReadColor,
             Stats,
             Layout,
             Present,
             SetWindow,
             Clear};
  return true;
}
