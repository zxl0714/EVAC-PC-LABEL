// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

#include "common/assert.h"

namespace Vulkan {

constexpr u64 WAIT_TIMEOUT = std::numeric_limits<u64>::max();

Semaphore::Semaphore(const Instance& instance_) : instance{instance_} {
    const vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
    };
    auto [semaphore_result, sem] =
        instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
    ASSERT_MSG(semaphore_result == vk::Result::eSuccess, "Failed to create master semaphore: {}",
               vk::to_string(semaphore_result));
    semaphore = std::move(sem);
}

Semaphore::~Semaphore() = default;

void Semaphore::Refresh() {
    auto known = gpu_tick.load(std::memory_order_acquire);
    // CAVE: an idle timeline cannot advance beyond the last reserved submit.
    // Draw calls still retire callbacks normally; skip only a driver query
    // that cannot reveal a newer completed tick. Pending submissions still poll.
    if (known >= current_tick.load(std::memory_order_acquire) - 1) { return; }
    host_queries.fetch_add(1, std::memory_order_relaxed);
    const auto [counter_result, counter] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
    ASSERT_MSG(counter_result == vk::Result::eSuccess,
               "Failed to get master semaphore value: {}", vk::to_string(counter_result));
    // Concurrent completion publication only needs max(), not another driver
    // query for every failed CAS. Never move a known completion backward.
    while (counter > known && !gpu_tick.compare_exchange_weak(known, counter, std::memory_order_release,
                                                              std::memory_order_relaxed)) {}
}

void Semaphore::Wait(u64 tick) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    while (instance.GetDevice().waitSemaphores(&wait_info, WAIT_TIMEOUT) != vk::Result::eSuccess) {
    }
    Refresh();
}

} // namespace Vulkan
