#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace kt::diagnostics {
// Whole records only. Producers never wait for a sink or allocate queue storage.
// Contention drops the incoming record; a full queue evicts its oldest record.
// Neither case can leave a truncated JSON object in the output.
template<std::size_t Capacity = 1024, std::size_t RecordBytes = 4096>
class AsyncLogQueue {
    static_assert(Capacity > 0 && RecordBytes > 0);
public:
    using Write = bool (*)(void*, std::string_view) noexcept;
    using Flush = bool (*)(void*) noexcept;
    struct Stats {
        std::uint64_t accepted{}, written{}, dropped_oldest{}, contended{}, oversized{}, failures{}, stopped{};
        std::size_t pending{}, in_flight{};
    };
    AsyncLogQueue(Write write, Flush flush, void* context)
        : write_(write), flush_(flush), context_(context) {
        if (!write_ || !flush_) throw std::invalid_argument("Log sink callbacks are required");
        worker_ = std::thread([this] { Run(); });
    }
    ~AsyncLogQueue() {
        // The owner must release a deliberately blocked test sink before
        // destruction. The runtime uses explicit drain and process ownership.
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    AsyncLogQueue(const AsyncLogQueue&) = delete;
    AsyncLogQueue& operator=(const AsyncLogQueue&) = delete;

    bool Submit(std::string_view text) noexcept {
        if (text.empty() || text.size() > RecordBytes) { ++oversized_; return false; }
        try {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock.owns_lock()) { ++contended_; return false; }
            if (stopping_) { ++stopped_; return false; }
            if (pending_ == Capacity) {
                head_ = (head_ + 1) % Capacity;
                --pending_; ++dropped_oldest_;
            }
            auto& item = items_[(head_ + pending_) % Capacity];
            item.size = text.size();
            std::memcpy(item.text.data(), text.data(), text.size());
            ++published_; ++pending_; ++accepted_;
            lock.unlock(); ready_.notify_one();
            return true;
        } catch (...) { ++failures_; return false; }
    }
    bool Drain(std::chrono::milliseconds timeout, bool stop = false) {
        std::unique_lock lock(mutex_);
        const auto target = published_;
        stopping_ |= stop;
        flush_requested_ = true;
        ready_.notify_one();
        const bool complete = drained_.wait_for(lock, timeout, [&] {
            return flushed_ >= target && (!stop || exited_);
        });
        lock.unlock();
        if (complete && stop && worker_.joinable()) worker_.join();
        return complete && failures_.load() == 0;
    }
    Stats Snapshot() const {
        std::lock_guard lock(mutex_);
        return {accepted_, written_, dropped_oldest_, contended_, oversized_, failures_, stopped_, pending_, in_flight_};
    }
private:
    struct Item { std::size_t size{}; std::array<char, RecordBytes> text; };
    void Run() noexcept {
        try { RunLoop(); }
        catch (...) {
            ++failures_;
            try {
                std::lock_guard lock(mutex_);
                stopping_ = true; exited_ = true; drained_.notify_all();
            } catch (...) { /* A failed diagnostic must not terminate the game. */ }
        }
    }
    void RunLoop() {
        for (;;) {
            Item item;
            std::uint64_t target{};
            bool finish{}, has_item{};
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return stopping_ || flush_requested_ || pending_; });
                if (pending_) {
                    const auto& front = items_[head_];
                    item.size = front.size;
                    std::memcpy(item.text.data(), front.text.data(), item.size);
                    head_ = (head_ + 1) % Capacity;
                    --pending_; in_flight_ = 1; has_item = true;
                } else {
                    target = published_; flush_requested_ = false; finish = stopping_;
                }
            }
            // No stdio/file operation runs under the producer mutex.
            if (has_item) {
                const bool written = write_(context_, {item.text.data(), item.size});
                std::lock_guard lock(mutex_);
                in_flight_ = 0;
                if (written) ++written_; else ++failures_;
                continue;
            }
            if (!flush_(context_)) ++failures_;
            {
                std::lock_guard lock(mutex_);
                flushed_ = target; exited_ = finish; drained_.notify_all();
            }
            if (finish) return;
        }
    }
    Write write_; Flush flush_; void* context_;
    mutable std::mutex mutex_;
    std::condition_variable ready_, drained_;
    std::array<Item, Capacity> items_;
    std::size_t head_{}, pending_{}, in_flight_{};
    std::uint64_t published_{}, flushed_{};
    std::atomic<std::uint64_t> accepted_{}, written_{}, dropped_oldest_{}, contended_{}, oversized_{}, failures_{}, stopped_{};
    bool stopping_{}, flush_requested_{}, exited_{};
    std::thread worker_;
};
}
