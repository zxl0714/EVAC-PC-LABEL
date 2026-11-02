// Isolated GPU-completion experiment. Uses Granite's existing external-timeline
// API and a D3D12 shared fence; no swapchain, game data or normal runtime change.
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "context.hpp"
#include "device.hpp"
#include "fence.hpp"
#include "thread_id.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result,const char* message) {if(FAILED(result))throw std::runtime_error(message);}
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Event {
    HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Event(){Require(value!=nullptr,"Cannot create completion event");}
    ~Event(){CloseHandle(value);}
};
uint64_t Cycles(){uint64_t value{};Require(QueryThreadCycleTime(GetCurrentThread(),&value),"Cannot read CPU cycles");return value;}
struct Sample {double wallUs{},waitCycles{};};
}
int main() {
    try {
        Util::register_thread_index(0);
        Require(Vulkan::Context::init_loader(nullptr),"Vulkan loader unavailable");
        Vulkan::Context context;context.set_num_thread_indices(1);
        Require(context.init_instance_and_device(nullptr,0,nullptr,0),"Vulkan device unavailable");
        Vulkan::Device gpu;gpu.set_context(context);gpu.init_frame_contexts(2);
        const auto& features=gpu.get_device_features();
        if(!features.supports_external || !features.vk11_props.deviceLUIDValid) {
            std::cout<<"{\"supported\":false,\"reason\":\"external semaphore or adapter LUID unavailable\"}\n";
            return 0;
        }
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> d3d;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Cannot create DXGI factory");
        LUID luid{};std::memcpy(&luid,features.vk11_props.deviceLUID,sizeof(luid));
        Check(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)),"Cannot match Vulkan adapter");
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d3d)),"Cannot open matching D3D12 device");
        ComPtr<ID3D12Fence> fence;
        Check(d3d->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"Cannot create shared fence");
        auto timeline=gpu.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT);
        if(!timeline) {
            std::cout<<"{\"supported\":false,\"reason\":\"D3D12 timeline unavailable\"}\n";
            return 0;
        }
        Vulkan::ExternalHandle shared;
        shared.semaphore_handle_type=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        Check(d3d->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&shared.handle),"Cannot share fence");
        if(!timeline->import_from_handle(shared)) {
            CloseHandle(shared.handle);
            throw std::runtime_error("Cannot import shared timeline");
        }
        Event completion;
        Vulkan::BufferCreateInfo bufferInfo{};
        bufferInfo.size=16u*1024u*1024u;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        auto buffer=gpu.create_buffer(bufferInfo);Require(bool(buffer),"Cannot create GPU workload buffer");
        bufferInfo.size=4;bufferInfo.domain=Vulkan::BufferDomain::CachedHost;
        bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        auto readback=gpu.create_buffer(bufferInfo);Require(bool(readback),"Cannot create verification buffer");
        uint64_t value=0;
        auto run=[&](bool windows,bool measure) {
            gpu.next_frame_context();
            const uint32_t pattern=0xc0000000u|uint32_t(++value);
            auto cmd=gpu.request_command_buffer();
            for(unsigned i=0;i<32;++i) {
                if(i)cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                cmd->fill_buffer(*buffer,pattern);
            }
            cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            cmd->copy_buffer(*readback,0,*buffer,0,4);
            cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
            // Both waits observe the same submission and timeline point. Arm
            // before submission to cover the signal-before-wait race as well.
            Check(fence->SetEventOnCompletion(value,completion.value),"Cannot arm completion event");
            gpu.submit(cmd);
            auto signal=gpu.request_timeline_semaphore_as_binary(*timeline,value);
            Vulkan::Fence normal;
            gpu.submit_empty(Vulkan::CommandBuffer::Type::Generic,&normal,signal.get());
            const auto before=Cycles();const auto start=std::chrono::steady_clock::now();
            if(windows)Require(WaitForSingleObject(completion.value,5000)==WAIT_OBJECT_0,"Windows GPU completion timed out");
            else {
                Require(normal->wait_timeout(5'000'000'000ull),"Vulkan GPU completion timed out");
                Require(WaitForSingleObject(completion.value,5000)==WAIT_OBJECT_0,"Shared completion was not signaled");
            }
            const auto end=std::chrono::steady_clock::now();const auto after=Cycles();
            const auto completed=fence->GetCompletedValue();
            Require(completed!=UINT64_MAX && completed>=value,"Shared fence woke early or device was removed");
            Require(normal->wait_timeout(5'000'000'000ull),"Native fence did not complete");
            auto* mapped=gpu.map_host_buffer(*readback,Vulkan::MEMORY_ACCESS_READ_BIT);
            uint32_t actual{};Require(mapped!=nullptr,"Cannot map verification buffer");
            std::memcpy(&actual,mapped,4);gpu.unmap_host_buffer(*readback,Vulkan::MEMORY_ACCESS_READ_BIT);
            Require(actual==pattern,"Completion preceded the expected GPU output");
            // SetEventOnCompletion must also work for an already completed value.
            Check(fence->SetEventOnCompletion(value,completion.value),"Cannot arm an already completed value");
            Require(WaitForSingleObject(completion.value,5000)==WAIT_OBJECT_0,"Already completed fence did not wake");
            return Sample{measure?std::chrono::duration<double,std::micro>(end-start).count():0.0,
                          measure?double(after-before):0.0};
        };
        for(unsigned i=0;i<16;++i)run(bool(i&1),false);
        unsigned batch=0;
        for(unsigned round=0;round<3;++round)for(const bool windows:{false,true,true,false}) {
            Sample total;
            for(unsigned i=0;i<80;++i){const auto row=run(windows,true);total.wallUs+=row.wallUs;total.waitCycles+=row.waitCycles;}
            std::cout<<std::fixed<<std::setprecision(3)<<"{\"batch\":"<<++batch
                <<",\"method\":\""<<(windows?"windows_event":"vulkan_wait")<<"\",\"samples\":80"
                <<",\"wait_wall_us\":"<<total.wallUs/80<<",\"wait_cpu_cycles\":"<<total.waitCycles/80
                <<",\"gpu_fill_bytes_per_sample\":536870912,\"verified_readback_bytes\":4,\"gameplay_measured\":false}\n";
        }
        gpu.wait_idle();
        std::cout<<"{\"supported\":true,\"output_verified\":true,\"completed_signal_and_wait_races\":true}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
