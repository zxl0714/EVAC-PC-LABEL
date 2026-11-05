#pragma once
// Graphics implementation only; no game-generated source includes this file.
#include "gs_device.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include "gs_completion_mailbox.h"
#include "fence.hpp"
#include <thread>

namespace cave::ps2 {
class GsDevice::Impl : public ParallelGS::SignalInterface {
public:
    Vulkan::Context context;
    Vulkan::Device ownedDevice;
    Vulkan::Device* device;
    ParallelGS::GSInterface gs;
    const std::thread::id owner=std::this_thread::get_id();
    std::string gpu;
    uint64_t readbacks=0;
    std::shared_ptr<GsCompletionMailbox> completions=std::make_shared<GsCompletionMailbox>();
    std::function<void()> waitPump;
    uint64_t commandGeneration=0,activeGeneration=1;
    Vulkan::Fence fieldFence;
    Vulkan::ImageHandle lastField;
    bool trackFieldBlits=false;
    struct BlitField { uint64_t generation=0; unsigned phase=0; uint64_t revision=0; };
    std::array<BlitField,512> blitFields{};
    uint64_t scanoutGeneration=1;
    unsigned completedBlitPage=0;
    uint64_t workSerial=0;
    Vulkan::ImageHandle fieldHistory[2][2];
    Vulkan::Program* fieldProgram=nullptr;
    void ObserveFieldBlit(unsigned path,std::span<const uint8_t> packet);
    Vulkan::ImageHandle ReconstructField(Vulkan::ImageHandle field,unsigned phase,VkImageLayout layout,
                                         VkPipelineStageFlags2 stage,VkAccessFlags2 access);
    explicit Impl(Vulkan::Device* borrowed=nullptr);
    ~Impl();
    void CheckOwner() const;
    void CheckRange(size_t offset,size_t bytes) const;
    void BeginOperation();
    void CompleteMarker();
    // Windowed callers flush before WSI acquire; offline callers flush directly.
    ParallelGS::ScanoutResult ScanoutFlushed(const GsFrame& frame,VkImageLayout layout,
                                      VkPipelineStageFlags2 stage,VkAccessFlags2 access,bool currentField=false);
    bool on_signal(uint64_t payload) override;
    bool on_finish(uint64_t payload) override;
    bool on_label(uint64_t payload) override;
};
}
