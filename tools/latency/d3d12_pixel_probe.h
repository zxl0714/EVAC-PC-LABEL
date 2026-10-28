#pragma once

// Private latency experiment. Capture a bounded ROI from the actual backbuffer
// on its original command list. No per-frame CPU/GPU wait or file IO. The copy
// still has a cost: ON/OFF controls are required before interpreting timings.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace evac_pixels {
inline uint64_t Clock() {
  LARGE_INTEGER value{};
  QueryPerformanceCounter(&value);
  return value.QuadPart;
}
struct Frame {
  uint64_t source_frame{}, source_packet{}, source_event{}, copy_qpc{};
  uint64_t present_begin{}, present_end{};
  uint32_t result{};
  bool signaled{}, presented{};
};
struct Capture {
  static constexpr unsigned kLimit = 1024;
  std::mutex mutex;
  bool configured{}, failed{}, closed{}, allocated{};
  unsigned x{}, y{}, width{}, height{}, capacity{}, row_pitch{}, stride{};
  unsigned full_width{}, full_height{}, format{}, count{}, overflow{};
  HANDLE data = INVALID_HANDLE_VALUE, metadata = INVALID_HANDLE_VALUE;
  Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  std::array<Frame, kLimit> frames{};

  Capture() {
    wchar_t prefix[32768]{}, region[128]{};
    const auto length = GetEnvironmentVariableW(L"EVAC_LATENCY_PIXELS", prefix, 32768);
    if (!length || length >= 32768) return;
    configured = true;
    wchar_t extra{};
    const auto region_length = GetEnvironmentVariableW(L"EVAC_LATENCY_PIXEL_REGION", region, 128);
    if (!region_length || region_length >= 128 ||
        swscanf_s(region, L"%u,%u,%u,%u,%u%c", &x, &y, &width, &height, &capacity, &extra, 1u) != 5 ||
        !width || !height || width > 1024 || height > 1024 ||
        !capacity || capacity > kLimit || uint64_t(width) * height * capacity > 100000000) {
      failed = true; return;
    }
    const std::wstring base(prefix);
    data = CreateFileW((base + L".bin").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    metadata = CreateFileW((base + L".csv").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (data == INVALID_HANDLE_VALUE || metadata == INVALID_HANDLE_VALUE) failed = true;
    row_pitch = (width * 4 + 255) & ~255u;
    stride = (row_pitch * height + 511) & ~511u;
  }

  // A token is one-based and only valid for the command list that copied it.
  unsigned Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* list,
      ID3D12Resource* source, uint64_t command, uint64_t source_frame,
      uint64_t source_packet, uint64_t source_event, bool drawn) {
    if (!configured) return 0;
    std::lock_guard lock(mutex);
    if (closed || failed) return 0;
    const auto desc = source->GetDesc();
    if (desc.Width < uint64_t(x) + width || desc.Height < uint64_t(y) + height) return 0;
    if (!allocated) {
      if (desc.SampleDesc.Count != 1 ||
          (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
           desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM)) { failed = true; return 0; }
      full_width = static_cast<unsigned>(desc.Width); full_height = desc.Height; format = desc.Format;
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_READBACK;
      D3D12_RESOURCE_DESC output{};
      output.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      output.Width = uint64_t(stride) * capacity;
      output.Height = 1; output.DepthOrArraySize = 1; output.MipLevels = 1;
      output.SampleDesc.Count = 1; output.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &output,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer))) ||
          FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        failed = true; return 0;
      }
      allocated = true;
    }
    if (desc.Width != full_width || desc.Height != full_height || static_cast<unsigned>(desc.Format) != format) {
      failed = true; return 0;
    }
    if (!drawn || command < 90 || command >= 1000) return 0;
    if (count == capacity) { ++overflow; return 0; }
    const auto index = count++;
    frames[index] = {source_frame, source_packet, source_event, Clock()};
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = source;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = buffer.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Offset = uint64_t(index) * stride;
    to.PlacedFootprint.Footprint = {desc.Format, width, height, 1, row_pitch};
    const D3D12_BOX box{x, y, 0, x + width, y + height, 1};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &barrier);
    return index + 1;
  }
  void Submitted(ID3D12CommandQueue* queue, unsigned token) {
    if (!token) return;
    std::lock_guard lock(mutex);
    if (closed || token > count) return;
    frames[token - 1].signaled = SUCCEEDED(queue->Signal(fence.Get(), token));
  }
  void Presented(unsigned token, uint64_t begin, uint64_t end, HRESULT result) {
    if (!token) return;
    std::lock_guard lock(mutex);
    if (closed || token > count) return;
    auto& frame = frames[token - 1];
    frame.present_begin = begin; frame.present_end = end;
    frame.result = static_cast<uint32_t>(result); frame.presented = true;
  }
  static bool WriteAll(HANDLE file, const void* bytes, size_t size) {
    const auto* pointer = static_cast<const unsigned char*>(bytes);
    while (size) {
      const auto block = static_cast<DWORD>(size > 1048576 ? 1048576 : size);
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
    uint64_t completed{};
    if (fence) {
      completed = fence->GetCompletedValue();
      if (completed < count && count && frames[count - 1].signaled) {
        const auto event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event && SUCCEEDED(fence->SetEventOnCompletion(count, event))) WaitForSingleObject(event, 5000);
        if (event) CloseHandle(event);
        completed = fence->GetCompletedValue();
      }
      if (completed == UINT64_MAX) { failed = true; completed = 0; }
    }
    unsigned valid{};
    for (; valid < count; ++valid)
      if (valid + 1 > completed || !frames[valid].signaled || !frames[valid].presented) break;
    bool bytes_ok = valid == count;
    if (buffer && valid && data != INVALID_HANDLE_VALUE) {
      void* mapping{};
      D3D12_RANGE read{0, size_t(stride) * valid};
      if (SUCCEEDED(buffer->Map(0, &read, &mapping))) {
        bytes_ok = WriteAll(data, mapping, read.End) && bytes_ok;
        const D3D12_RANGE written{0, 0}; buffer->Unmap(0, &written);
      } else bytes_ok = false;
    }
    if (metadata != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
      char line[1024];
      auto size = std::snprintf(line, sizeof(line),
          "# evac_pixels_v1 frequency=%llu pid=%lu width=%u height=%u x=%u y=%u roi_width=%u roi_height=%u format=%u pitch=%u stride=%u capacity=%u\n"
          "index,source_frame,source_packet,source_event,copy_qpc,present_begin,present_end,hr\n",
          static_cast<unsigned long long>(frequency.QuadPart), GetCurrentProcessId(), full_width,
          full_height, x, y, width, height, format, row_pitch, stride, capacity);
      bool ok = WriteAll(metadata, line, size);
      for (unsigned i = 0; ok && i < valid; ++i) {
        const auto& f = frames[i];
        size = std::snprintf(line, sizeof(line), "%u,%llu,%llu,%llu,%llu,%llu,%llu,%u\n", i,
            static_cast<unsigned long long>(f.source_frame), static_cast<unsigned long long>(f.source_packet),
            static_cast<unsigned long long>(f.source_event), static_cast<unsigned long long>(f.copy_qpc),
            static_cast<unsigned long long>(f.present_begin), static_cast<unsigned long long>(f.present_end), f.result);
        ok = WriteAll(metadata, line, size);
      }
      if (ok) {
        size = std::snprintf(line, sizeof(line), "# end captured=%u valid=%u overflow=%u failed=%u bytes_ok=%u\n",
            count, valid, overflow, failed, bytes_ok);
        WriteAll(metadata, line, size);
      }
    }
    if (data != INVALID_HANDLE_VALUE) CloseHandle(data);
    if (metadata != INVALID_HANDLE_VALUE) CloseHandle(metadata);
  }
};
// Retain COM objects through process exit, including quick_exit. No game thread
// waits on the readback. Flush only occurs during the existing teardown path.
inline Capture& Pixels() { static auto* value = new Capture; return *value; }
inline void Flush() { Pixels().Flush(); }
inline uint64_t HostCommand() {
  using Function = uint64_t (*)();
  static const auto function = reinterpret_cast<Function>(
      GetProcAddress(GetModuleHandleW(nullptr), "EvacLatencyTestCommand"));
  return function ? function() : 0;
}
}  // namespace evac_pixels

extern "C" __declspec(dllexport) void EvacLatencyPixelsFlush() {
  evac_pixels::Flush();
}
