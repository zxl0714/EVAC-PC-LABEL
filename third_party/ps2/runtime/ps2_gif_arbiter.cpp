#include "runtime/gs/ps2_gif_arbiter.h"
#include <algorithm>
#include <cstring>

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    // FLG=3 ("disabled" in the GS manual) behaves exactly like IMAGE: NLOOP qwords of image data.
    return flg == 2u || flg == 3u;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    GifArbiterPacket pkt;
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.resize(sizeBytes);
    std::memcpy(pkt.data.data(), data, sizeBytes);
    m_queue.push_back(std::move(pkt));
}

void GifArbiter::drain()
{
    if (!m_processFn)
        return;

    // A strict rank preserves the intended DIRECTHL exception. The old pairwise
    // comparator made plain PATH2 and DIRECTHL equivalent but ordered them
    // differently against PATH3 IMAGE, violating strict weak ordering.
    const auto rank = [](const GifArbiterPacket &packet) {
        if (packet.pathId == GifPathId::Path1) return 0;
        if (packet.pathId == GifPathId::Path2 && !packet.path2DirectHl) return 1;
        if (packet.path3Image) return 2;
        if (packet.pathId == GifPathId::Path2) return 3;
        return 4;
    };
    std::stable_sort(m_queue.begin(), m_queue.end(),
        [&](const auto &a, const auto &b) { return rank(a) < rank(b); });

    for (size_t i = 0; i < m_queue.size(); ++i)
    {
        auto &pkt = m_queue[i];
        if (!pkt.data.empty())
        {
            m_processFn(pkt.pathId, pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
        }
    }
    m_queue.clear();
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
