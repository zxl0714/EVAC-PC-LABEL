#pragma once
#include <cstdint>

namespace guwange::performance {
inline bool RenderQueueIdle(std::uint32_t cleanup, std::uint32_t cleanup_head,
                            std::uint32_t resources_write, std::uint32_t resources_read,
                            std::uint32_t frames_write, std::uint32_t frames_read,
                            std::uint32_t flush, std::uint32_t stopping) {
    return cleanup != 0 && cleanup_head == 0 &&
        resources_write == resources_read && frames_write == frames_read &&
        flush == 0 && stopping == 0;
}
}
