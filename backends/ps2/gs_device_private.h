#pragma once
// Graphics implementation only; no game-generated source includes this file.
#include "gs_device.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include <thread>

namespace cave::ps2 {
class GsDevice::Impl {
public:
    Vulkan::Context context;
    Vulkan::Device ownedDevice;
    Vulkan::Device* device;
    ParallelGS::GSInterface gs;
    const std::thread::id owner=std::this_thread::get_id();
    std::string gpu;
    uint64_t readbacks=0;
    explicit Impl(Vulkan::Device* borrowed=nullptr);
    ~Impl();
    void CheckOwner() const;
    void CheckRange(size_t offset,size_t bytes) const;
};
}
