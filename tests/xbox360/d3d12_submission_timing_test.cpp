#include <rex/graphics/d3d12/submission_timing.h>
#include <d3d12sdklayers.h>
#include <windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using rex::graphics::d3d12::SubmissionTiming;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Check(HRESULT hr) { Require(SUCCEEDED(hr), "D3D12 API failed"); }

int main() {
  try {
    ComPtr<ID3D12Debug> debug;
    const bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (validation) debug->EnableDebugLayer();
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;
    device.As(&messages);
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    Check(list->Close());
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "CreateEvent failed");
    SubmissionTiming timing;
    Require(timing.Initialize(device.Get(), queue.Get()), "Initialize timing");
    uint64_t serial = 0, collected = 0;
    auto submit = [&] {
      Check(allocator->Reset()); Check(list->Reset(allocator.Get(), nullptr));
      const int slot = timing.Begin(list.Get());
      timing.End(list.Get(), slot, {++serial, serial, 0, 2.5, 0, 0, true});
      Check(list->Close());
      ID3D12CommandList* lists[] = {list.Get()};
      queue->ExecuteCommandLists(1, lists); Check(queue->Signal(fence.Get(), serial));
      Check(fence->SetEventOnCompletion(serial, event));
      Require(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "Test submission completed");
    };
    auto consume = [&](const SubmissionTiming::Sample& sample) {
      Require(sample.submission > collected, "Ordered completion");
      collected = sample.submission;
      Require(sample.frame == sample.submission && sample.closes_frame, "Metadata survives readback");
      Require(std::isfinite(sample.gpu_us) && sample.gpu_us >= 0, "Valid GPU time");
      Require(sample.encode_us == 2.5, "CPU encode time remains separate");
    };
    for (unsigned i = 0; i < SubmissionTiming::kSlots + 3; ++i) submit();
    Require(timing.pending() == SubmissionTiming::kSlots && timing.dropped() == 3, "Full ring drops diagnostics only");
    timing.Collect(0, consume);
    Require(collected == 0, "Incomplete fence does not expose readback");
    timing.Collect(UINT64_MAX, consume);
    Require(collected == 0, "Device removal is not completion");
    timing.Collect(SubmissionTiming::kSlots - 1, consume);
    Require(collected == SubmissionTiming::kSlots - 1 && timing.pending() == 1, "Partial completion respects fence");
    timing.Collect(fence->GetCompletedValue(), consume);
    Require(collected == SubmissionTiming::kSlots && timing.pending() == 0, "All completed data collected");
    for (unsigned i = 0; i < 16; ++i) { submit(); timing.Collect(fence->GetCompletedValue(), consume); }
    Require(collected == serial && timing.invalid() == 0, "Ring reuse preserves timestamps");
    if (messages) for (uint64_t i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T size = 0; Check(messages->GetMessage(i, nullptr, &size));
      std::vector<unsigned char> storage(size);
      auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
      Check(messages->GetMessage(i, message, &size));
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        std::cerr << message->pDescription << '\n'; throw std::runtime_error("D3D12 validation error");
      }
    }
    CloseHandle(event);
    std::cout << "GPU timing passed: " << serial << " submissions, validation=" << validation << ", dropped=3, invalid=0\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
