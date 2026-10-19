#include "native_async_log.h"
#include "diagnostics/async_log_queue.h"
#include <windows.h>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Queue = kt::diagnostics::AsyncLogQueue<8, 128>;
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Sink {
    std::mutex mutex;
    std::condition_variable changed;
    bool block{}, entered{}, released{}, fail{}, flush_fail{};
    std::vector<std::string> records;
    static bool Write(void* context, std::string_view text) noexcept {
        auto& sink = *static_cast<Sink*>(context);
        try {
            std::unique_lock lock(sink.mutex);
            sink.entered = true; sink.changed.notify_all();
            sink.changed.wait(lock, [&] { return !sink.block || sink.released; });
            sink.records.emplace_back(text);
            return !sink.fail;
        } catch (...) { return false; }
    }
    static bool Flush(void* context) noexcept { return !static_cast<Sink*>(context)->flush_fail; }
};
struct ReleaseSink {
    Sink& sink;
    ~ReleaseSink() { std::lock_guard lock(sink.mutex); sink.released = true; sink.changed.notify_all(); }
};
template<class T> void FirstRecord(T& queue, std::string_view text) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!queue.Submit(text)) {
        Require(std::chrono::steady_clock::now() < deadline, "initial enqueue timed out");
        std::this_thread::yield();
    }
}
}

bool NativeAsyncLogSelfTest() {
    try {
        Sink sink; sink.block = true;
        auto queue = std::make_unique<Queue>(Sink::Write, Sink::Flush, &sink);
        ReleaseSink release_on_failure{sink};
        FirstRecord(*queue, "first\n");
        {
            std::unique_lock lock(sink.mutex);
            Require(sink.changed.wait_for(lock, std::chrono::seconds(5), [&] { return sink.entered; }), "worker did not enter slow sink");
        }
        // The sink is demonstrably blocked until this producer finishes.
        // This proves separation, without a flaky microsecond performance limit.
        for (unsigned index = 0; index < 1000; ++index)
            Require(queue->Submit(std::to_string(index) + "\n"), "producer waited for blocked sink");
        const auto full = queue->Snapshot();
        Require(full.pending == 8 && full.in_flight == 1 && full.dropped_oldest == 992, "fixed capacity/newest retention");
        Require(!queue->Submit(std::string(129, 'x')), "oversized record was truncated");
        Require(!queue->Drain(std::chrono::milliseconds(20)), "blocked sink falsely reported drained");
        { std::lock_guard lock(sink.mutex); sink.released = true; sink.changed.notify_all(); }
        Require(queue->Drain(std::chrono::seconds(5), true), "drain after release");
        const auto complete = queue->Snapshot();
        Require(complete.pending == 0 && complete.in_flight == 0 && complete.written == 9 && complete.oversized == 1, "retained record accounting");
        Require(sink.records.front() == "first\n" && sink.records.back() == "999\n", "latest tail was not retained");
        Require(!queue->Submit("after stop\n") && queue->Snapshot().stopped == 1, "stopped queue accepted data");

        Sink failed; failed.fail = true;
        auto failing = std::make_unique<Queue>(Sink::Write, Sink::Flush, &failed);
        FirstRecord(*failing, "write failure\n");
        Require(!failing->Drain(std::chrono::seconds(5), true), "write failure falsely reported success");
        Require(failing->Snapshot().failures == 1, "write failure counter");
        Sink unflushed; unflushed.flush_fail = true;
        auto flush_failure = std::make_unique<Queue>(Sink::Write, Sink::Flush, &unflushed);
        Require(!flush_failure->Drain(std::chrono::seconds(5), true), "flush failure falsely reported success");

        Sink concurrent;
        auto shared = std::make_unique<kt::diagnostics::AsyncLogQueue<1024, 128>>(Sink::Write, Sink::Flush, &concurrent);
        std::array<std::thread, 4> producers;
        for (unsigned producer = 0; producer < producers.size(); ++producer) producers[producer] = std::thread([&, producer] {
            for (unsigned index = 0; index < 100; ++index)
                (void)shared->Submit(std::to_string(producer) + ":" + std::to_string(index) + "\n");
        });
        for (auto& producer : producers) producer.join();
        Require(shared->Drain(std::chrono::seconds(5), true), "multi-producer drain");
        const auto counts = shared->Snapshot();
        Require(counts.accepted + counts.contended == 400 && counts.written == counts.accepted && !counts.dropped_oldest, "multi-producer accounting");
        Require(std::set<std::string>(concurrent.records.begin(), concurrent.records.end()).size() == counts.written, "record corruption or duplication");

        Require(StartNativeAsyncLog() && !StartNativeAsyncLog(), "runtime queue initialization");
        errno = 42; SetLastError(1234);
        (void)NativeLogf("{\"event\":\"async_log_fixture\",\"value\":%u}\n", 17u);
        Require(errno == 42 && GetLastError() == 1234, "logging changed host errors");
        (void)WriteNativeLog("{\"event\":\"async_log_fixture_raw\"}\n");
        const std::string oversized(5000, 'x');
        Require(NativeLogf("%s", oversized.c_str()) == -1, "formatter published oversized fragment");
        Require(StopNativeAsyncLog(), "runtime drain");
        Require(!WriteNativeLog("{\"event\":\"after_stop_fixture\"}\n"), "runtime resumed synchronous output after stop");
        PrintNativeAsyncLogDiagnostics();
        std::puts("{\"event\":\"async_log_self_test\",\"passed\":true,\"slow_sink_producer_separation\":true,\"bounded_newest_retention\":true,\"whole_records\":true}");
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Async log self-test: %s\n", error.what());
        return false;
    }
}
