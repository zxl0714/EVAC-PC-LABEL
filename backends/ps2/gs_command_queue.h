#pragma once
#include "gs_command_backend.h"
#include <chrono>
#include <exception>
#include <memory>

namespace cave::ps2 {
// FIFO transport only. The window thread supplies a consumer and owns all GS /
// Vulkan state. Payloads are copied before enqueue returns. Capacity includes
// the command currently executing, not just the pending deque.
class GsCommandQueue final : public GsCommandBackend {
public:
    struct Stats {
        size_t pendingBytes=0,commands=0,highWaterBytes=0,highWaterCommands=0;
        uint64_t submitted=0,completed=0;
        bool frameOutstanding=false,closed=false;
    };
    explicit GsCommandQueue(size_t capacityBytes=8u*1024u*1024u,size_t capacityCommands=2048);
    ~GsCommandQueue() override;
    GsCommandQueue(const GsCommandQueue&)=delete;
    GsCommandQueue& operator=(const GsCommandQueue&)=delete;
    // Call on exactly one consumer thread. Returns false on timeout or when
    // closed. A consumer exception closes the queue and reaches all waiters.
    bool ConsumeOne(GsCommandBackend& consumer,std::chrono::milliseconds timeout);
    void Close(std::exception_ptr failure={});
    Stats GetStats() const;

    void Initialize(std::span<const uint8_t> vram) override;
    void Reset() override;
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override;
    void Register(uint8_t address,uint64_t value) override;
    void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) override;
    size_t ReadFifo(std::span<uint8_t> bytes) override;
    void Snapshot(std::vector<uint8_t>& bytes) override;
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override;
    // Frame is a submission boundary: only one frame request can be outstanding.
    // The consumer must also gate next-frame GPU commands on its previous fence.
    void Frame(const GsFrame& request) override;
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
