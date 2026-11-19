#pragma once
#include "vulkan_headers.hpp"
#include <cstdio>

namespace cave { namespace ps2 {
// The window thread arms this only for the loading frame and the first game
// present on a borrowed swapchain. Ordinary frames go straight to Vulkan.
struct HostedPresentTrace {
    static HostedPresentTrace*& Active() noexcept {
        static thread_local HostedPresentTrace* sample{};
        return sample;
    }
    const char* phase;
    unsigned generation;
    const char* rtss;
    bool called{};

    HostedPresentTrace(const char* phase, unsigned generation, const char* rtss) noexcept
        : phase(phase), generation(generation), rtss(rtss), previous(Active()) { Active() = this; }
    ~HostedPresentTrace() { Active() = previous; }
    HostedPresentTrace(const HostedPresentTrace&) = delete;
    HostedPresentTrace& operator=(const HostedPresentTrace&) = delete;

    VkResult Present(PFN_vkQueuePresentKHR present, VkQueue queue, const VkPresentInfoKHR& info,
                     size_t imageCount, VkPresentModeKHR mode, uint64_t presentId) {
        called = true;
        const char* id = "none";
        const char* fence = "none";
        bool modeOverride = false, timings = false;
        for (auto* p = static_cast<const VkBaseInStructure*>(info.pNext); p; p = p->pNext) {
            switch (p->sType) {
            case VK_STRUCTURE_TYPE_PRESENT_ID_KHR: id = "id"; break;
            case VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR: id = "id2"; break;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR: {
                const auto* f = reinterpret_cast<const VkSwapchainPresentFenceInfoKHR*>(p);
                fence = f->swapchainCount == 1 && f->pFences && f->pFences[0] ? "set" : "null";
                break;
            }
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR: modeOverride = true; break;
            case VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT: timings = true; break;
            default: break;
            }
        }
        const bool hasImage = info.swapchainCount == 1 && info.pImageIndices;
        const bool hasWait = info.waitSemaphoreCount && info.pWaitSemaphores && info.pWaitSemaphores[0];
        std::fprintf(stderr, "[ps2-dl-wsi] vkQueuePresentKHR enter phase=%s generation=%u serial=%llu "
            "mode=%d image_index=%u image_count=%zu queue_nonnull=%d swapchain_nonnull=%d wait_count=%u wait0_nonnull=%d "
            "id_extension=%s fence=%s mode_override=%d timings=%d rtss=%s\n",
            phase, generation, static_cast<unsigned long long>(presentId), int(mode),
            hasImage ? info.pImageIndices[0] : UINT32_MAX, imageCount, queue != VK_NULL_HANDLE,
            info.swapchainCount == 1 && info.pSwapchains && info.pSwapchains[0], info.waitSemaphoreCount,
            hasWait, id, fence, modeOverride, timings, rtss);
        std::fflush(stderr); // Preserve the input even if the driver never returns.
        const VkResult result = present(queue, &info);
        if (info.pResults && info.swapchainCount == 1 && (result >= 0 || info.pResults[0] != VK_SUCCESS)) {
            std::fprintf(stderr, "[ps2-dl-wsi] vkQueuePresentKHR return phase=%s generation=%u "
                "result=%d image_result=%d\n", phase, generation, int(result), int(info.pResults[0]));
        } else {
            // Granite initializes pResults to success. A failed call may leave it untouched.
            std::fprintf(stderr, "[ps2-dl-wsi] vkQueuePresentKHR return phase=%s generation=%u "
                "result=%d image_result=unknown\n", phase, generation, int(result));
        }
        std::fflush(stderr);
        return result;
    }

private:
    HostedPresentTrace* previous;
};
}}
