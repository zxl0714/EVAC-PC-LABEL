#pragma once

// Private measurement only. Copy the same guest image as the owned snapshot,
// on the existing command buffer. No added queue submit, GPU wait or frame IO.
// The existing copied fence proves completion; write only after presenter drain.
#include "ps4_probe_exports.h"
#include "video_core/buffer_cache/buffer.h"
#include <array>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>

namespace evac_vk_pixels {
inline std::uint64_t Clock() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}
inline std::uint64_t HostCommand() {
  using Function = std::uint64_t (*)();
  static const auto function = reinterpret_cast<Function>(
      GetProcAddress(GetModuleHandleW(nullptr), "EvacLatencyTestCommand"));
  return function ? function() : 0;
}
struct Frame {
  std::uint64_t prepare{}, copy_qpc{}, ready{}, present_begin{}, present_end{};
  std::uint32_t result{};
  bool completed{};
};
struct Capture {
  static constexpr unsigned Limit = 1024;
  bool configured{}, failed{}, closed{};
  unsigned x{}, y{}, width{}, height{}, capacity{}, pitch{}, stride{};
  unsigned full_width{}, full_height{}, format{}, count{}, overflow{};
  HANDLE data = INVALID_HANDLE_VALUE, metadata = INVALID_HANDLE_VALUE;
  std::array<Frame, Limit> frames{};
  std::unique_ptr<VideoCore::Buffer> buffer;
  std::mutex mutex;

  Capture() {
    wchar_t prefix[32768]{}, region[128]{}, extra{};
    const auto length = GetEnvironmentVariableW(L"EVAC_LATENCY_PIXELS", prefix, 32768);
    if (!length || length >= 32768) return;
    configured = true;
    const auto region_length = GetEnvironmentVariableW(L"EVAC_LATENCY_PIXEL_REGION", region, 128);
    if (!region_length || region_length >= 128 ||
        swscanf_s(region, L"%u,%u,%u,%u,%u%c", &x, &y, &width, &height, &capacity, &extra, 1u) != 5 ||
        !width || !height || width > 1024 || height > 1024 ||
        !capacity || capacity > Limit || std::uint64_t(width) * height * capacity > 100000000) {
      failed = true; return;
    }
    pitch = width * 4;
    stride = (pitch * height + 255) & ~255u;
    const std::wstring base(prefix);
    data = CreateFileW((base + L".bin").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    metadata = CreateFileW((base + L".csv").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (data == INVALID_HANDLE_VALUE || metadata == INVALID_HANDLE_VALUE) failed = true;
  }
  unsigned Prepare(const Vulkan::Instance& instance, vk::CommandBuffer command,
      vk::Image source, vk::Extent2D size, vk::Format source_format,
      std::uint64_t prepare, std::uint64_t host_command) {
    if (!configured || host_command < 90 || host_command >= 1000) return 0;
    std::lock_guard lock(mutex);
    if (closed || failed) return 0;
    if (std::uint64_t(x) + width > size.width || std::uint64_t(y) + height > size.height || !prepare ||
        (source_format != vk::Format::eR8G8B8A8Srgb && source_format != vk::Format::eB8G8R8A8Srgb)) {
      failed = true; return 0;
    }
    if (!buffer) {
      full_width = size.width; full_height = size.height;
      format = static_cast<unsigned>(source_format);
      buffer = std::make_unique<VideoCore::Buffer>(instance, 0,
          std::uint64_t(stride) * capacity, VideoCore::MemoryType::HostCached, "Latency ROI");
      if (buffer->mapped_data.size() != std::uint64_t(stride) * capacity) {
        failed = true; return 0;
      }
    }
    if (size.width != full_width || size.height != full_height ||
        static_cast<unsigned>(source_format) != format) { failed = true; return 0; }
    if (count == capacity) { ++overflow; return 0; }
    const auto token = ++count;
    frames[token - 1] = {prepare, Clock()};
    const vk::BufferImageCopy copy{
        .bufferOffset = std::uint64_t(token - 1) * stride,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {static_cast<int>(x), static_cast<int>(y), 0},
        .imageExtent = {width, height, 1}};
    // source is already TransferSrcOptimal from RecordSnapshot. The snapshot
    // and this ROI read the same immutable source during this submission.
    command.copyImageToBuffer(source, vk::ImageLayout::eTransferSrcOptimal, buffer->Handle(), copy);
    const vk::BufferMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer->Handle(),
        .offset = copy.bufferOffset,
        .size = std::uint64_t(pitch) * height};
    command.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    return token;
  }
  void Completed(unsigned token) {
    if (!token) return;
    std::lock_guard lock(mutex);
    if (token > count || closed) { failed = true; return; }
    frames[token - 1].completed = true;
  }
  void Ready(unsigned token, std::uint64_t ready) {
    if (!token) return;
    std::lock_guard lock(mutex);
    if (token > count || closed || !frames[token - 1].completed || !ready) { failed = true; return; }
    frames[token - 1].ready = ready;
  }
  void Presented(unsigned token, std::uint64_t begin, std::uint64_t end, std::uint32_t result) {
    if (!token) return;
    std::lock_guard lock(mutex);
    if (token > count || closed || !frames[token - 1].ready || !begin || !end ||
        frames[token - 1].present_begin) { failed = true; return; }
    auto& frame = frames[token - 1];
    frame.present_begin = begin; frame.present_end = end; frame.result = result;
  }
  static bool Write(HANDLE file, const void* bytes, std::size_t size) {
    const auto* pointer = static_cast<const unsigned char*>(bytes);
    while (size) {
      const auto block = static_cast<DWORD>(std::min<std::size_t>(size, 1048576));
      DWORD written{};
      if (!WriteFile(file, pointer, block, &written, nullptr) || written != block) return false;
      pointer += block; size -= block;
    }
    return true;
  }
  void Flush() {
    std::lock_guard lock(mutex);
    if (closed) return;
    closed = true;
    if (!configured) return;
    unsigned valid{};
    for (; valid < count && frames[valid].completed; ++valid) {}
    bool bytes_ok = valid == count;
    if (buffer && valid && data != INVALID_HANDLE_VALUE) {
      buffer->Invalidate(0, std::uint64_t(valid) * stride);
      bytes_ok = Write(data, buffer->mapped_data.data(), std::uint64_t(valid) * stride) && bytes_ok;
    }
    if (metadata != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
      char line[1024];
      auto size = std::snprintf(line, sizeof(line),
          "# evac_vk_pixels_v1 frequency=%llu pid=%lu width=%u height=%u x=%u y=%u roi_width=%u roi_height=%u format=%u pitch=%u stride=%u capacity=%u\n"
          "index,prepare,copy_qpc,ready,present_begin,present_end,result\n",
          static_cast<unsigned long long>(frequency.QuadPart), GetCurrentProcessId(), full_width, full_height,
          x, y, width, height, format, pitch, stride, capacity);
      bool ok = Write(metadata, line, size);
      for (unsigned i = 0; ok && i < valid; ++i) {
        const auto& f = frames[i];
        size = std::snprintf(line, sizeof(line), "%u,%llu,%llu,%llu,%llu,%llu,%u\n", i,
            static_cast<unsigned long long>(f.prepare), static_cast<unsigned long long>(f.copy_qpc),
            static_cast<unsigned long long>(f.ready), static_cast<unsigned long long>(f.present_begin),
            static_cast<unsigned long long>(f.present_end), f.result);
        ok = Write(metadata, line, size);
      }
      if (ok) {
        size = std::snprintf(line, sizeof(line), "# end captured=%u valid=%u overflow=%u failed=%u bytes_ok=%u\n",
            count, valid, overflow, failed, bytes_ok);
        Write(metadata, line, size);
      }
    }
    if (data != INVALID_HANDLE_VALUE) CloseHandle(data);
    if (metadata != INVALID_HANDLE_VALUE) CloseHandle(metadata);
  }
};
} // namespace evac_vk_pixels
