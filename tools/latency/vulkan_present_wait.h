#pragma once

// Private observer. The render thread never waits for presentation completion.
// KHR present-wait reports an implementation-defined completion upper bound,
// not a hardware timestamp or physical panel response.
#include <windows.h>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace evac_latency {
class VulkanPresentWait {
    struct Request {
        std::uint64_t id{}, source{}, begin{}, end{}, observed{};
        int result{};
    };
    vk::Device device;
    vk::SwapchainKHR chain;
    std::FILE* file{};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Request> pending;
    std::vector<Request> completed;
    std::thread worker;
    bool stopping{};
    std::uint64_t next{}, dropped{};
    vk::PresentIdKHR ids{};

    static std::uint64_t Now() {
        LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc); return qpc.QuadPart;
    }
    void Run() {
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopping || !pending.empty(); });
                if (pending.empty()) return;
                request = pending.front(); pending.pop_front();
            }
            // Use the raw entry point: error codes are evidence, not exceptions
            // escaping a worker. At most 50 ms per submitted observation.
            request.result = VULKAN_HPP_DEFAULT_DISPATCHER.vkWaitForPresentKHR(
                static_cast<VkDevice>(device), static_cast<VkSwapchainKHR>(chain),
                request.id, 50'000'000);
            request.observed = Now();
            completed.push_back(request);
        }
    }

public:
    VulkanPresentWait(vk::Device device_, vk::SwapchainKHR chain_,
                      const std::filesystem::path& path) : device(device_), chain(chain_) {
        if (std::filesystem::exists(path) || _wfopen_s(&file, path.c_str(), L"wx") || !file)
            throw std::runtime_error("Fresh present-wait output required");
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        std::fprintf(file, "{\"kind\":\"header\",\"pid\":%lu,\"frequency\":%llu,\"completion_upper_bound\":true}\n",
                     GetCurrentProcessId(), static_cast<unsigned long long>(frequency.QuadPart));
        completed.reserve(16384);
        worker = std::thread([this] { Run(); });
    }
    VulkanPresentWait(const VulkanPresentWait&) = delete;
    VulkanPresentWait& operator=(const VulkanPresentWait&) = delete;
    ~VulkanPresentWait() { Stop(); }

    const void* Before(const void* previous = nullptr) {
        ++next;
        ids = {.pNext = previous, .swapchainCount = 1, .pPresentIds = &next};
        return &ids;
    }
    void After(std::uint64_t source, std::uint64_t begin, std::uint64_t end,
               vk::Result result) {
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) return;
        std::lock_guard lock(mutex);
        if (stopping || next > 16384 || pending.size() >= 8) { ++dropped; return; }
        pending.push_back({next, source, begin, end});
        changed.notify_one();
    }
    void Stop() {
        if (!file) return;
        {
            std::lock_guard lock(mutex); stopping = true;
        }
        changed.notify_one();
        if (worker.joinable()) worker.join();
        // Must be called before swapchain retirement/destruction. No file IO on
        // the frame path; all records are written only after the worker joins.
        for (const auto& r : completed)
            std::fprintf(file, "{\"kind\":\"wait\",\"id\":%llu,\"source\":%llu,\"begin\":%llu,\"end\":%llu,\"observed\":%llu,\"result\":%d}\n",
                         r.id, r.source, r.begin, r.end, r.observed, r.result);
        std::fprintf(file, "{\"kind\":\"end\",\"requests\":%llu,\"completed\":%zu,\"dropped\":%llu}\n",
                     next, completed.size(), dropped);
        std::fclose(file); file = nullptr;
    }
};
} // namespace evac_latency
