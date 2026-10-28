#include "d3d12_pixel_probe.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <stdexcept>

static void Check(HRESULT result) {
  if (FAILED(result)) throw std::runtime_error("D3D12 pixel mechanism failure");
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  SetEnvironmentVariableW(L"EVAC_LATENCY_PIXELS", argv[1]);
  SetEnvironmentVariableW(L"EVAC_LATENCY_PIXEL_REGION", L"4,4,8,8,4");
  Microsoft::WRL::ComPtr<ID3D12Debug> debug;
  Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
  debug->EnableDebugLayer();
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
  Microsoft::WRL::ComPtr<ID3D12Resource> texture;
  D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = texture_desc.Height = 32;
  texture_desc.DepthOrArraySize = texture_desc.MipLevels = texture_desc.SampleDesc.Count = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture)));
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv_desc.NumDescriptors = 1;
  Check(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv)));
  const auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(texture.Get(), nullptr, handle);
  evac_pixels::Capture capture;
  const float colors[4][4] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,1,1}};
  if (capture.Prepare(device.Get(), list.Get(), texture.Get(), 0, 0, 0, 0, true)) return 3;
  for (unsigned i=0; i<4; ++i) {
    list->ClearRenderTargetView(handle, colors[i], 0, nullptr);
    if (capture.Prepare(device.Get(), list.Get(), texture.Get(), 101+i, 200+i, 300+i, 400+i, true) != i+1) return 4;
  }
  if (capture.Prepare(device.Get(), list.Get(), texture.Get(), 105, 0, 0, 0, true)) return 5;
  Check(list->Close());
  ID3D12CommandList* submit = list.Get(); queue->ExecuteCommandLists(1, &submit);
  for (unsigned i=1; i<=4; ++i) {
    capture.Submitted(queue.Get(), i);
    const auto qpc = evac_pixels::Clock(); capture.Presented(i, qpc, qpc, S_OK);
  }
  capture.Flush();
  Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
  Check(device.As(&info));
  for (UINT64 i=0; i<info->GetNumStoredMessages(); ++i) {
    SIZE_T size{}; Check(info->GetMessage(i, nullptr, &size));
    auto memory = std::make_unique<unsigned char[]>(size);
    auto* message = reinterpret_cast<D3D12_MESSAGE*>(memory.get());
    Check(info->GetMessage(i, message, &size));
    if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
      std::fprintf(stderr, "%s\n", message->pDescription); return 6;
    }
  }
  std::puts("Four ordered GPU ROI copies, capacity rejection and D3D12 validation passed");
}
