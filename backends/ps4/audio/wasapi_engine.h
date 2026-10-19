#pragma once
#include <cstdint>
#include <memory>

namespace kt::audio {
struct Configuration { unsigned frames, channels; bool floating; bool standard8 = false; float master_gain = 1.0f; };
struct Snapshot {
    std::uint64_t stream_id{};
    std::uint64_t submitted_frames{}, written_frames{}, consumed_frames{}, underruns{};
    std::uint64_t submissions{}, last_submit_clock_us{}, last_underrun_clock_us{};
    std::uint64_t submit_gap_us{}, submit_wait_us{}, max_submit_gap_us{}, max_submit_wait_us{}, max_pump_gap_us{};
    unsigned padding{}, device_capacity{}, waiting{};
    bool pending{}, closed{}, retired{};
    long failure{};
};
struct Port;
class WasapiEngine {
public:
    WasapiEngine();
    ~WasapiEngine();
    WasapiEngine(const WasapiEngine&) = delete;
    WasapiEngine& operator=(const WasapiEngine&) = delete;
    long ReadyResult() const;
    std::shared_ptr<Port> Open(Configuration configuration, long& result);
    static long Submit(const std::shared_ptr<Port>& port, const void* data);
    static long Volume(const std::shared_ptr<Port>& port, unsigned flags, int value);
    static long Close(const std::shared_ptr<Port>& port);
    static Snapshot Inspect(const std::shared_ptr<Port>& port);
    void Stop();
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
};
bool AudioConversionSelfTest();
}
