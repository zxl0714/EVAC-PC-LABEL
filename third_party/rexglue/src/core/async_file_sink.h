#pragma once
// Bounded file logging with an explicit flush barrier. No networking or polling.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/sinks/sink.h>

namespace rex::logging {
class AsyncFileSink final : public spdlog::sinks::sink {
public:
    explicit AsyncFileSink(spdlog::sink_ptr output,std::size_t capacity=4096)
        : output_(std::move(output)),capacity_(capacity) {
        if (!output_ || !capacity_) throw std::invalid_argument("Invalid async file sink");
        worker_=std::thread([this]{Run();});
    }
    ~AsyncFileSink() override {
        {std::lock_guard lock(mutex_);stopping_=true;}
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    void log(const spdlog::details::log_msg& message) override {
        auto bounded=message;
        bounded.payload=spdlog::string_view_t(message.payload.data(),std::min<std::size_t>(message.payload.size(),16384));
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        if (queue_.size()==capacity_) {queue_.pop_front();++dropped_;}
        queue_.push_back(Item{++published_,spdlog::details::log_msg_buffer(bounded)});
        ready_.notify_one();
    }
    void flush() override {
        std::unique_lock lock(mutex_);
        const auto target=published_;
        flush_requested_=true;ready_.notify_one();
        // Bound explicit-marker/fatal waiting; normal log() never waits for IO.
        drained_.wait_for(lock,std::chrono::seconds(2),[&]{return flushed_>=target;});
    }
    void set_pattern(const std::string& pattern) override {output_->set_pattern(pattern);}
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {output_->set_formatter(std::move(formatter));}
    std::uint64_t dropped() const {return dropped_.load();}
    std::size_t pending() const {std::lock_guard lock(mutex_);return queue_.size();}
private:
    struct Item {std::uint64_t sequence;spdlog::details::log_msg_buffer message;};
    void Run() noexcept {
        std::uint64_t reported_drops{};
        while (true) {
            std::optional<Item> item;
            std::uint64_t flush_prefix{};
            bool finish{};
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock,[&]{return stopping_ || flush_requested_ || !queue_.empty();});
                if (!queue_.empty()) {item.emplace(std::move(queue_.front()));queue_.pop_front();}
                else {flush_prefix=completed_;flush_requested_=false;finish=stopping_;}
            }
            if (item) {
                try {output_->log(item->message);} catch (...) {++failures_;}
                std::lock_guard lock(mutex_);completed_=item->sequence;
                continue;
            }
            try {
                const auto total=dropped_.load();
                if (total!=reported_drops) {
                    const auto text=fmt::format("LOG_QUEUE dropped_oldest={} capacity={} newest_records_retained=true",total,capacity_);
                    output_->log(spdlog::details::log_msg("logging",spdlog::level::warn,text));
                    reported_drops=total;
                }
                output_->flush();
            } catch (...) {++failures_;}
            {
                std::lock_guard lock(mutex_);flushed_=std::max(flushed_,flush_prefix);
                drained_.notify_all();
            }
            if (finish) return;
        }
    }
    spdlog::sink_ptr output_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_,drained_;
    std::deque<Item> queue_;
    std::uint64_t published_{},completed_{},flushed_{};
    std::atomic<std::uint64_t> dropped_{},failures_{};
    bool stopping_{},flush_requested_{};
    std::thread worker_;
};
}
