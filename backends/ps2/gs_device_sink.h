#pragma once
#include "gs_command_backend.h"
#include "gs_device.h"
#include <functional>

namespace cave::ps2 {
// Synchronous consumer used by offline replay and the graphics owner. The game
// producer will use the bounded queue client, not call a foreign-thread device.
class GsDeviceSink final : public GsCommandBackend {
public:
    GsDeviceSink(GsDevice& device,std::function<void(const GsFrame&)> frame,
                 std::function<void()> beginFrame={});
    void BeginCommand(uint64_t generation) override;
    GsCompletions CollectCompletions() override;
    void AcknowledgeSignal() override;
    void AcknowledgeStatus(uint32_t mask) override;
    void Initialize(std::span<const uint8_t> vram) override;
    void Reset() override;
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override;
    void Register(uint8_t address,uint64_t value) override;
    void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) override;
    size_t ReadFifo(std::span<uint8_t> bytes) override;
    void Snapshot(std::vector<uint8_t>& bytes) override;
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override;
    void Frame(const GsFrame& request) override;
private:
    GsDevice& device;
    std::function<void(const GsFrame&)> frame;
    std::function<void()> beginFrame;
    bool beginNeeded=true;
    void BeforeCommand();
};
}
