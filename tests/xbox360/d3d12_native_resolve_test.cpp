// Explicit GPU test of the production shader. No ROM or guest runtime needed.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "third_party/rexglue/src/graphics/shaders/bytecode/d3d12_5_1/direct_rgba8_resolve_cs.h"

using Microsoft::WRL::ComPtr;
static void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
static void Check(HRESULT hr) { Require(SUCCEEDED(hr), "D3D12 operation failed"); }

// Reference is the existing texture_util::GetTiledOffset2D formula with bpp=4,
// evaluated on CPU independently of the compiled production shader.
static uint32_t Offset(uint32_t x, uint32_t y, uint32_t pitch) {
  uint32_t macro = ((x >> 5) + (y >> 5) * ((pitch + 31) >> 5)) << 9;
  uint32_t micro = ((x & 7) + ((y & 14) << 2)) << 2;
  uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 1) << 4);
  return ((offset & ~511u) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
}

int main() {
  try {
    ComPtr<ID3D12Debug> debug;
    bool validation = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (validation) debug->EnableDebugLayer();
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;
    device.As(&messages);
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                  IID_PPV_ARGS(&list)));
    Check(list->Close());
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(completed != nullptr, "CreateEvent");

    constexpr uint32_t kSourceWidth = 128, kSourceHeight = 128, kOutputSize = 2 * 1024 * 1024;
    auto buffer = [&](uint32_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
      ComPtr<ID3D12Resource> result;
      D3D12_HEAP_PROPERTIES properties{};
      properties.Type = heap;
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width = bytes; desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
      desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      desc.Flags = flags;
      Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state,
          nullptr, IID_PPV_ARGS(&result)));
      return result;
    };
    auto upload = buffer(kOutputSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = buffer(kOutputSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto destination = buffer(kOutputSize, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    std::vector<uint32_t> pixels(kSourceWidth * kSourceHeight);
    for (uint32_t y = 0; y < kSourceHeight; ++y) {
      for (uint32_t x = 0; x < kSourceWidth; ++x) {
        pixels[y * kSourceWidth + x] = ((x * 3 + y * 7) & 255) |
            (((x * 11 + y) & 255) << 8) | (((x + y * 13) & 255) << 16) |
            (((x * 17 + y * 19) & 255) << 24);
      }
    }
    auto source_upload = buffer(uint32_t(pixels.size() * 4), D3D12_HEAP_TYPE_UPLOAD,
                                D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped;
    D3D12_RANGE no_read{};
    Check(source_upload->Map(0, &no_read, &mapped));
    std::memcpy(mapped, pixels.data(), pixels.size() * 4);
    source_upload->Unmap(0, nullptr);
    Check(upload->Map(0, &no_read, &mapped));
    std::memset(mapped, 0xCD, kOutputSize);
    upload->Unmap(0, nullptr);
    D3D12_HEAP_PROPERTIES default_heap{}; default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC source_desc{};
    source_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    source_desc.Width = kSourceWidth; source_desc.Height = kSourceHeight;
    source_desc.DepthOrArraySize = source_desc.MipLevels = source_desc.SampleDesc.Count = 1;
    source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12Resource> source;
    Check(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &source_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&source)));

    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = ranges[1].NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 10;
    for (unsigned i = 1; i < 3; ++i) {
      parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i].DescriptorTable = {1, &ranges[i - 1]};
    }
    D3D12_ROOT_SIGNATURE_DESC root_desc{3, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> root_blob, errors;
    Check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &root_blob, &errors));
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                      IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root.Get();
    pipeline_desc.CS = {direct_rgba8_resolve_cs, sizeof(direct_rgba8_resolve_cs)};
    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)));
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc{};
    descriptor_desc.NumDescriptors = 2;
    descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    Check(device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors)));
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    uav.Buffer.NumElements = kOutputSize / 4;
    device->CreateUnorderedAccessView(destination.Get(), nullptr, &uav, cpu);
    cpu.ptr += increment;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device->CreateShaderResourceView(source.Get(), &srv, cpu);
    auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after) {
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
      list->ResourceBarrier(1, &barrier);
    };
    // Includes non-group-aligned dimensions, source/destination subrectangles,
    // destination pitch padding and every accepted endianness + channel swap.
    const std::array<std::array<uint32_t, 8>, 4> regions{{
      {{0,0,64,64,0,0,64,0}}, {{1,2,56,40,8,24,96,4096}},
      {{7,11,8,8,120,120,160,8192}}, {{0,8,127,79,24,8,192,12288}}}};
    uint64_t serial = 0;
    for (const auto& region : regions) for (uint32_t endian = 0; endian < 4; ++endian)
    for (uint32_t swap = 0; swap < 2; ++swap) {
      Check(allocator->Reset()); Check(list->Reset(allocator.Get(), pipeline.Get()));
      if (!serial) {
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = source.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = source_upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, kSourceWidth, kSourceHeight, 1, kSourceWidth * 4};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        transition(source.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      } else transition(destination.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->CopyBufferRegion(destination.Get(), 0, upload.Get(), 0, kOutputSize);
      transition(destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      ID3D12DescriptorHeap* heaps[] = {descriptors.Get()}; list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(root.Get());
      uint32_t constants[10]; std::memcpy(constants, region.data(), sizeof(region));
      constants[8] = endian; constants[9] = swap;
      list->SetComputeRoot32BitConstants(0, 10, constants, 0);
      auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
      list->SetComputeRootDescriptorTable(1, gpu); gpu.ptr += increment;
      list->SetComputeRootDescriptorTable(2, gpu);
      list->Dispatch((region[2] + 31) / 32, (region[3] + 7) / 8, 1);
      transition(destination.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.Get(), 0, destination.Get(), 0, kOutputSize);
      Check(list->Close()); ID3D12CommandList* lists[] = {list.Get()};
      queue->ExecuteCommandLists(1, lists); Check(queue->Signal(fence.Get(), ++serial));
      Check(fence->SetEventOnCompletion(serial, completed));
      Require(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0, "GPU timeout");
      std::vector<uint32_t> expected(kOutputSize / 4, 0xCDCDCDCD);
      for (uint32_t y = 0; y < region[3]; ++y) for (uint32_t x = 0; x < region[2]; ++x) {
        uint32_t rgba = pixels[(region[1] + y) * kSourceWidth + region[0] + x];
        if (swap) rgba = (rgba & 0xFF00FF00) | ((rgba & 255) << 16) | ((rgba >> 16) & 255);
        std::array<uint8_t, 4> bytes;
        std::memcpy(bytes.data(), &rgba, 4);
        const uint32_t mask = endian == 1 ? 1 : endian == 2 ? 3 : endian == 3 ? 2 : 0;
        uint32_t output = 0;
        for (uint32_t b = 0; b < 4; ++b) output |= uint32_t(bytes[b ^ mask]) << (8 * b);
        const auto offset = region[7] + Offset(region[4] + x, region[5] + y, region[6]);
        Require(offset + 4 <= kOutputSize, "Reference output bounds");
        expected[offset / 4] = output;
      }
      D3D12_RANGE read{0, kOutputSize}; Check(readback->Map(0, &read, &mapped));
      const bool same = std::memcmp(mapped, expected.data(), kOutputSize) == 0;
      readback->Unmap(0, &no_read);
      Require(same, "Shader output or untouched padding differs from CPU reference");
    }
    CloseHandle(completed);
    if (messages) for (UINT64 n = 0; n < messages->GetNumStoredMessages(); ++n) {
      SIZE_T size = 0; messages->GetMessage(n, nullptr, &size);
      std::vector<uint8_t> bytes(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
      Check(messages->GetMessage(n, message, &size));
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        std::cerr << message->pDescription << '\n'; throw std::runtime_error("D3D12 validation error");
      }
    }
    std::cout << serial << " native resolve cases passed; validation=" << validation << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
