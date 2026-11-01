#pragma once
#include "gs_command_backend.h"
#include <filesystem>
#include <memory>

namespace cave::ps2 {
struct GsTraceStats {
    uint64_t commands=0,frames=0,bytes=0;
    bool finished=false,limited=false;
};
// Diagnostic-only recording at the original command boundary. Start with the
// backend's first Initialize; a VRAM-only snapshot mid-game is insufficient.
class GsRecordingBackend final : public GsCommandBackend {
public:
    GsRecordingBackend(std::unique_ptr<GsCommandBackend> target,const std::filesystem::path& path,
                        uint64_t maxBytes=256u*1024u*1024u,uint64_t maxFrames=3600);
    ~GsRecordingBackend() override;
    GsTraceStats Stats() const;
    void Finish();
    void BeginCommand(uint64_t generation) override;
    GsCompletions CollectCompletions() override;
    void AcknowledgeStatus(uint32_t mask) override;
    void AcknowledgeSignal() override;
    void Initialize(std::span<const uint8_t> vram) override;
    void Reset() override;
    void Gif(GifPathId path,std::span<const uint8_t> bytes) override;
    void Register(uint8_t address,uint64_t value) override;
    void Image(const std::array<uint64_t,4>& setup,std::span<const uint8_t> bytes) override;
    size_t ReadFifo(std::span<uint8_t> bytes) override;
    void Snapshot(std::vector<uint8_t>& bytes) override;
    void WriteVram(size_t offset,std::span<const uint8_t> bytes) override;
    void Frame(const GsFrame& request) override;
    bool TryFrame(const GsFrame& request) override;
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
// Bounded, versioned little-endian data. No host pointers or native struct
// padding. The consumer is a graphics-only replay sink: disable EE status waits
// because the captured command sequence already reflects EE's decisions.
class GsTraceWorkload {
public:
    explicit GsTraceWorkload(const std::filesystem::path& path,uint64_t maxBytes=1024ull*1024u*1024u);
    ~GsTraceWorkload();
    GsTraceWorkload(const GsTraceWorkload&)=delete;
    GsTraceWorkload& operator=(const GsTraceWorkload&)=delete;
    GsTraceStats Stats() const;
    // Decoded once at load. Reuse without disk I/O or command-buffer allocation
    // in the measured loop. One caller owns replay and its reusable FIFO scratch.
    GsTraceStats Replay(GsCommandBackend& target);
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
GsTraceStats ReplayGsTrace(const std::filesystem::path& path,GsCommandBackend& target,
                           uint64_t maxBytes=1024ull*1024u*1024u);
}
