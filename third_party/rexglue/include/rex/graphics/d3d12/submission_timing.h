// Optional developer-only GPU timing. No waits or extra queue submissions.
#pragma once
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace rex::graphics::d3d12 {
class SubmissionTiming {
 public:
  static constexpr unsigned kSlots = 256;
  struct Sample {
    uint64_t submission = 0, frame = 0;
    double gpu_us = 0, encode_us = 0;
    uint32_t draws = 0, copies = 0;
    bool closes_frame = false;
  };
  ~SubmissionTiming() {
    if (mapped_) {
      const D3D12_RANGE written{0, 0};
      readback_->Unmap(0, &written);
    }
  }
  bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (FAILED(queue->GetTimestampFrequency(&frequency_)) || !frequency_) return false;
    D3D12_QUERY_HEAP_DESC heap{};
    heap.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap.Count = kSlots * 2;
    if (FAILED(device->CreateQueryHeap(&heap, IID_PPV_ARGS(&queries_)))) return false;
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = kSlots * 2 * sizeof(uint64_t);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
        &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)))) return false;
    const D3D12_RANGE read{0, static_cast<SIZE_T>(buffer.Width)};
    return SUCCEEDED(readback_->Map(0, &read, reinterpret_cast<void**>(&mapped_)));
  }

  // The caller reclaims completed slots before recording. A full ring drops
  // telemetry, never waits or reuses a query/readback location still in flight.
  int Begin(ID3D12GraphicsCommandList* list) {
    if (tail_ - head_ == kSlots) { ++dropped_; return -1; }
    const unsigned slot = unsigned(tail_ % kSlots);
    list->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
    return int(slot);
  }
  void End(ID3D12GraphicsCommandList* list, int slot, Sample sample) {
    if (slot < 0) return;
    list->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, unsigned(slot) * 2 + 1);
    list->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        unsigned(slot) * 2, 2, readback_.Get(), unsigned(slot) * 2 * sizeof(uint64_t));
    samples_[unsigned(slot)] = sample;
    ++tail_;
  }
  template <typename Consumer>
  void Collect(uint64_t completed_submission, Consumer consume) {
    // GetCompletedValue returns UINT64_MAX for device removal, not completion.
    if (completed_submission == UINT64_MAX) return;
    while (head_ != tail_) {
      const unsigned slot = unsigned(head_ % kSlots);
      if (samples_[slot].submission > completed_submission) break;
      Sample sample = samples_[slot];
      const uint64_t start = mapped_[slot * 2], end = mapped_[slot * 2 + 1];
      ++head_;
      if (!start || end < start) { ++invalid_; continue; }
      sample.gpu_us = double(end - start) * 1e6 / double(frequency_);
      consume(sample);
    }
  }
  uint64_t dropped() const { return dropped_; }
  uint64_t invalid() const { return invalid_; }
  uint64_t pending() const { return tail_ - head_; }

 private:
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries_;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
  uint64_t* mapped_ = nullptr;
  uint64_t frequency_ = 0, head_ = 0, tail_ = 0, dropped_ = 0, invalid_ = 0;
  std::array<Sample, kSlots> samples_{};
};
}  // namespace rex::graphics::d3d12
