#ifndef PS2_GIF_ARBITER_H
#define PS2_GIF_ARBITER_H

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

enum class GifPathId : uint8_t
{
    Path1 = 1,
    Path2 = 2,
    Path3 = 3,
};

struct GifTransferRange
{
    GifPathId path;
    uint32_t size;
};

struct GifArbiterPacket
{
    GifPathId pathId;
    bool path2DirectHl = false;
    bool path3Image = false;
    std::vector<uint8_t> data;
};

class GifArbiter
{
public:
    using ProcessPacketFn = std::function<void(GifPathId, const uint8_t *, uint32_t)>;

    using ProcessBurstFn = std::function<void(std::span<const GifTransferRange>, std::span<const uint8_t>)>;
    void setProcessBurstFn(ProcessBurstFn fn) { m_processBurst = std::move(fn); }

    GifArbiter() = default;
    explicit GifArbiter(ProcessPacketFn processFn);

    void setProcessPacketFn(ProcessPacketFn fn) { m_processFn = std::move(fn); }

    void submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl = false);
    void submitImmediate(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl = false);

    // VU1 executes synchronously on the EE thread. The raw GS consumes a
    // byte stream. Its ordered ranges can share transport up to the size
    // limit or return to the EE. Each GIF path retains its own parser.
    template<class Run> void runVifBurst(Run run)
    {
        if (m_inVifBurst) { run(); return; }
        m_inVifBurst = true;
        try {
            run();
            flushVifBurst();
        } catch (...) {
            m_burstBytes.clear();
            m_ranges.clear();
            m_inVifBurst = false;
            throw;
        }
        m_inVifBurst = false;
    }

    void flushVifBurst();
    void vifFence();
    void drain();
    [[nodiscard]] std::vector<uint8_t> saveState() const;
    void loadState(std::span<const uint8_t> state);
    bool empty() const { return m_queue.empty(); }

private:
    ProcessPacketFn m_processFn;
    std::vector<GifArbiterPacket> m_queue;
    bool m_inVifBurst = false;
    ProcessBurstFn m_processBurst;
    std::vector<uint8_t> m_burstBytes;
    std::vector<GifTransferRange> m_ranges;
    bool m_newRange = false;

    static bool isImagePacket(const uint8_t *data, uint32_t sizeBytes);
    static uint8_t pathPriority(GifPathId id);
};

#endif
