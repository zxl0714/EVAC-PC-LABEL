#ifndef PS2_GIF_ARBITER_H
#define PS2_GIF_ARBITER_H

#include <cstdint>
#include <functional>
#include <vector>

enum class GifPathId : uint8_t
{
    Path1 = 1,
    Path2 = 2,
    Path3 = 3,
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

    GifArbiter() = default;
    explicit GifArbiter(ProcessPacketFn processFn);

    void setProcessPacketFn(ProcessPacketFn fn) { m_processFn = std::move(fn); }

    void submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl = false);
    void submitImmediate(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl = false);

    // VU1 executes synchronously on the EE thread. The raw GS consumes a
    // byte stream, so adjacent completed XGKICK packets can share a transfer.
    template<class Run> void runPath1Burst(Run run)
    {
        if (m_inPath1Burst) { run(); return; }
        m_inPath1Burst = true;
        try {
            run();
            flushPath1();
        } catch (...) {
            m_path1.clear();
            m_inPath1Burst = false;
            throw;
        }
        m_inPath1Burst = false;
    }

    void drain();
    bool empty() const { return m_queue.empty(); }

private:
    ProcessPacketFn m_processFn;
    std::vector<GifArbiterPacket> m_queue;
    bool m_inPath1Burst = false;
    std::vector<uint8_t> m_path1;
    void flushPath1();

    static bool isImagePacket(const uint8_t *data, uint32_t sizeBytes);
    static uint8_t pathPriority(GifPathId id);
};

#endif
