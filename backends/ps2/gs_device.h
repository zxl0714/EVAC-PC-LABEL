#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Vulkan { class Device; }

namespace cave::ps2 {
enum class GsGifPath : uint8_t { Path1=1, Path2=2, Path3=3 };

// One GS command owner. Calls consume their payload before returning; any
// cross-thread producer must enqueue an owned copy before calling this API.
// No game, window, input or CPU-renderer state is owned here.
class GsDevice {
public:
    static constexpr size_t VramBytes=4u*1024u*1024u;
    GsDevice();
    ~GsDevice();
    GsDevice(const GsDevice&)=delete;
    GsDevice& operator=(const GsDevice&)=delete;
    const std::string& GpuName() const noexcept;
    uint64_t ReadbackRequests() const;
    void GifTransfer(GsGifPath path,std::span<const uint8_t> payload);
    void WriteRegister(uint32_t address,uint64_t value);
    void UploadImage(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes);
    void WriteVram(size_t offset,std::span<const uint8_t> bytes);
    void ReadTransferFifo(std::span<uint8_t> bytes);
    void Flush();
    void Reset();
    // Explicitly synchronous diagnostics. Never the normal presentation route.
    void ReadbackVram(size_t offset,std::span<uint8_t> bytes);
private:
    friend class GsDisplay;
    explicit GsDevice(Vulkan::Device& device);
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
