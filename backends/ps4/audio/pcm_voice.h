#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace kt::audio {
enum class PcmType { Signed16LE, Float32LE };

// A borrowed, interleaved PCM source. Its bytes must remain alive and unchanged
// until Configure succeeds again or the voice is destroyed. No output device,
// guest handles, codec enums, or platform callbacks belong to this component.
class PcmVoice {
public:
    bool Configure(std::span<const std::byte> data, PcmType type, unsigned channels, unsigned sample_rate,
                   std::size_t loop_begin = 0, std::size_t loop_end = 0, std::uint32_t repeats = 0);
    bool SetPitch(double ratio);
    bool SetGain(float left, float right);
    void Play();
    bool PlayFrom(double source_phase);
    bool SetNextFrame(std::span<const std::byte> frame);
    double EndPhase() const { return end_phase_; }
    void Pause() { playing_ = false; }
    void Resume() { playing_ = configured_ && position_ < static_cast<double>(frames_); }
    void Stop();
    // Adds into existing float samples; never clips individual voices. Invalid
    // destinations leave both destination and source position untouched.
    bool Mix(std::span<float> destination, unsigned output_channels, unsigned output_rate);
    bool Playing() const { return playing_; }
    double Position() const { return position_; }
private:
    float Sample(std::size_t frame, unsigned channel) const;
    float ReadSample(std::span<const std::byte> data, std::size_t frame, unsigned channel) const;
    void Advance(double step);
    std::span<const std::byte> data_;
    std::span<const std::byte> next_frame_;
    PcmType type_{};
    unsigned channels_ = 0, rate_ = 0;
    std::size_t frames_ = 0, loop_begin_ = 0, loop_end_ = 0;
    std::uint32_t repeats_ = 0, remaining_ = 0;
    double position_ = 0, pitch_ = 1;
    double end_phase_ = 0;
    float gain_[2]{1, 1};
    bool configured_ = false, playing_ = false;
};
bool PcmVoiceSelfTest();
}
