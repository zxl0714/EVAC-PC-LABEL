#include "pcm_voice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace kt::audio {
bool PcmVoice::Configure(std::span<const std::byte> data, PcmType type, unsigned channels, unsigned rate,
                         std::size_t begin, std::size_t end, std::uint32_t repeats) {
    if ((type != PcmType::Signed16LE && type != PcmType::Float32LE) || (channels != 1 && channels != 2) ||
        rate < 8000 || rate > 192000 || data.empty()) { return false; }
    const auto stride = channels * (type == PcmType::Signed16LE ? 2u : 4u);
    const auto frames = data.size() / stride;
    if (data.size() % stride || frames > (std::uint64_t{1} << 48)) { return false; }
    if (!end) { end = frames; }
    if (begin >= end || end > frames) { return false; }
    data_ = data; type_ = type; channels_ = channels; rate_ = rate; frames_ = frames;
    loop_begin_ = begin; loop_end_ = end; repeats_ = repeats; remaining_ = repeats;
    position_ = end_phase_ = 0; next_frame_ = {}; pitch_ = 1; gain_[0] = gain_[1] = 1; configured_ = true; playing_ = false;
    return true;
}
bool PcmVoice::SetPitch(double ratio) {
    if (!std::isfinite(ratio) || ratio <= 0 || ratio > 16) { return false; }
    pitch_ = ratio; return true;
}
bool PcmVoice::SetGain(float left, float right) {
    if (!std::isfinite(left) || !std::isfinite(right)) { return false; }
    gain_[0] = left; gain_[1] = right; return true;
}
void PcmVoice::Stop() { position_ = end_phase_ = 0; remaining_ = repeats_; playing_ = false; }
void PcmVoice::Play() { Stop(); playing_ = configured_; }
bool PcmVoice::PlayFrom(double phase) {
    if (!configured_ || !std::isfinite(phase) || phase < 0 || phase > (std::uint64_t{1} << 48)) { return false; }
    Stop(); position_ = phase; playing_ = true; Advance(0); return true;
}
bool PcmVoice::SetNextFrame(std::span<const std::byte> frame) {
    const auto stride = channels_ * (type_ == PcmType::Signed16LE ? 2u : 4u);
    if (!frame.empty() && (!configured_ || frame.size() != stride)) { return false; }
    next_frame_ = frame; return true;
}
float PcmVoice::ReadSample(std::span<const std::byte> data, std::size_t frame, unsigned channel) const {
    const auto index = frame * channels_ + channel;
    if (type_ == PcmType::Signed16LE) {
        std::int16_t value; std::memcpy(&value, data.data() + index * 2, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    float value; std::memcpy(&value, data.data() + index * 4, sizeof(value));
    return std::isfinite(value) ? value : 0;
}
float PcmVoice::Sample(std::size_t frame, unsigned channel) const { return ReadSample(data_, frame, channel); }
void PcmVoice::Advance(double step) {
    position_ += step;
    if (remaining_ && position_ >= static_cast<double>(loop_end_)) {
        const auto period = static_cast<double>(loop_end_ - loop_begin_);
        auto wraps = static_cast<std::uint64_t>(std::floor((position_ - static_cast<double>(loop_end_)) / period)) + 1;
        if (remaining_ != UINT32_MAX) { wraps = std::min<std::uint64_t>(wraps, remaining_); remaining_ -= static_cast<std::uint32_t>(wraps); }
        position_ -= static_cast<double>(wraps) * period;
    }
    if (position_ >= static_cast<double>(frames_)) { end_phase_ = position_ - static_cast<double>(frames_); position_ = static_cast<double>(frames_); playing_ = false; }
}
bool PcmVoice::Mix(std::span<float> destination, unsigned channels, unsigned rate) {
    if ((channels != 1 && channels != 2) || rate < 8000 || rate > 192000 || destination.size() % channels) { return false; }
    if (!playing_) { return true; }
    const double step = static_cast<double>(rate_) / rate * pitch_;
    for (std::size_t offset = 0; offset < destination.size() && playing_; offset += channels) {
        const auto frame = static_cast<std::size_t>(position_);
        auto next = (std::min)(frame + 1, frames_ - 1);
        if (remaining_ && next == loop_end_) { next = loop_begin_; }
        const float fraction = static_cast<float>(position_ - static_cast<double>(frame));
        double samples[2];
        for (unsigned channel = 0; channel < channels_; ++channel) {
            const double a = Sample(frame, channel);
            const double b = !remaining_ && frame + 1 == frames_ && !next_frame_.empty()
                ? ReadSample(next_frame_, 0, channel) : Sample(next, channel);
            samples[channel] = a + (b - a) * fraction;
        }
        if (channels_ == 1) { samples[1] = samples[0]; }
        const auto accumulate = [](float& target, double value) {
            // Keep numerical overflow out of the next voice. This is not an
            // audio-range limiter: ordinary sums above +/-1 remain unchanged.
            const double sum = (std::isfinite(target) ? target : 0) + value;
            const double limit = (std::numeric_limits<float>::max)();
            target = static_cast<float>(std::clamp(sum, -limit, limit));
        };
        if (channels == 1) { accumulate(destination[offset], (samples[0] * gain_[0] + samples[1] * gain_[1]) * 0.5); }
        else { accumulate(destination[offset], samples[0] * gain_[0]); accumulate(destination[offset + 1], samples[1] * gain_[1]); }
        Advance(step);
    }
    return true;
}

bool PcmVoiceSelfTest() {
    bool passed = true;
    auto check = [&](bool condition, const char* name) {
        std::printf("{\"event\":\"pcm_voice_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, condition ? "true" : "false");
        passed &= condition;
    };
    const std::array<std::int16_t, 4> pcm{-32768, 16384, 32767, 0};
    PcmVoice voice;
    std::array<float, 10> output{};
    check(voice.Configure(std::as_bytes(std::span(pcm)), PcmType::Signed16LE, 1, 48000), "configure_borrowed_pcm16");
    voice.Play(); output.back() = 73;
    check(voice.Mix(std::span(output).first(8), 2, 48000) && output[0] == -1 && output[1] == -1 &&
          output[2] == 0.5f && output[4] == 32767.0f / 32768 && output[7] == 0 && output.back() == 73 && !voice.Playing(), "exact_samples_mono_to_stereo_and_sentinel");
    voice.Play(); output.fill(0);
    check(!voice.Mix(std::span(output).first(3), 2, 48000) && voice.Position() == 0 && output[0] == 0 &&
          !voice.Configure(std::as_bytes(std::span(pcm)).first(3), PcmType::Signed16LE, 1, 48000) && voice.Playing(), "invalid_calls_are_transactional");
    voice.Pause(); (void)voice.Mix(output, 1, 48000);
    check(output[0] == 0 && voice.Position() == 0, "pause_preserves_position");
    voice.Resume(); (void)voice.Mix(std::span(output).first(1), 1, 48000);
    check(output[0] == -1 && voice.Position() == 1, "resume_advances_existing_voice");
    voice.Stop(); check(!voice.Playing() && voice.Position() == 0, "stop_rewinds");
    const std::array<float, 3> ramp{0, 1, 0};
    (void)voice.Configure(std::as_bytes(std::span(ramp)), PcmType::Float32LE, 1, 24000);
    voice.Play(); output.fill(0); (void)voice.Mix(std::span(output).first(6), 1, 48000);
    check(output[0] == 0 && output[1] == 0.5f && output[2] == 1 && output[3] == 0.5f && !voice.Playing(), "linear_resampling_and_natural_end");
    const std::array<float, 4> stereo{0.25f, -0.5f, 1, -1};
    (void)voice.Configure(std::as_bytes(std::span(stereo)), PcmType::Float32LE, 2, 48000);
    (void)voice.SetGain(2, 0.5f); voice.Play(); output.fill(0); (void)voice.Mix(std::span(output).first(2), 1, 48000);
    check(output[0] == 0.125f && output[1] == 0.75f, "stereo_to_mono_and_independent_gains");
    const std::array<float, 4> sequence{1, 2, 3, 4};
    (void)voice.Configure(std::as_bytes(std::span(sequence)), PcmType::Float32LE, 1, 48000, 1, 3, 1);
    voice.Play(); output.fill(0); (void)voice.Mix(std::span(output).first(6), 1, 48000);
    check(output[0] == 1 && output[1] == 2 && output[2] == 3 && output[3] == 2 && output[4] == 3 && output[5] == 4 && !voice.Playing(), "finite_loop_then_tail");
    (void)voice.Configure(std::as_bytes(std::span(sequence)), PcmType::Float32LE, 1, 48000, 1, 3, UINT32_MAX);
    (void)voice.SetPitch(0.5); voice.Play(); auto split = voice;
    std::array<float, 31> whole{}, parts{}; (void)voice.Mix(whole, 1, 48000);
    (void)split.Mix(std::span(parts).first(7), 1, 48000); (void)split.Mix(std::span(parts).subspan(7), 1, 48000);
    check(whole == parts && voice.Position() == split.Position() && voice.Playing(), "infinite_loop_fractional_phase_survives_block_boundaries");
    const std::array<float, 3> nonfinite{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -0.25f};
    (void)voice.Configure(std::as_bytes(std::span(nonfinite)), PcmType::Float32LE, 1, 48000);
    voice.Play(); output.fill(0); (void)voice.Mix(std::span(output).first(3), 1, 48000);
    check(output[0] == 0 && output[1] == 0 && output[2] == -0.25f && !voice.SetPitch(0) &&
          !voice.SetPitch(std::numeric_limits<double>::infinity()) && !voice.SetGain(0, std::numeric_limits<float>::quiet_NaN()), "finite_output_and_parameter_validation");
    voice.Play(); auto second = voice; output.fill(0);
    (void)voice.Mix(std::span(output).first(3), 1, 48000); (void)second.Mix(std::span(output).first(3), 1, 48000);
    check(output[2] == -0.5f, "independent_voices_accumulate");
    const auto limit = (std::numeric_limits<float>::max)();
    const std::array<float, 2> extreme{limit, -limit};
    (void)voice.Configure(std::as_bytes(std::span(extreme)), PcmType::Float32LE, 1, 48000);
    (void)voice.SetGain(limit, limit); voice.Play(); output.fill(0);
    (void)voice.Mix(std::span(output).first(2), 1, 48000);
    check(output[0] == limit && output[1] == -limit, "finite_interpolation_and_accumulation_at_numeric_extremes");
    const std::array<float, 5> continuous{0, 1, 2, 3, 4};
    PcmVoice whole_voice, stream_voice;
    (void)whole_voice.Configure(std::as_bytes(std::span(continuous)), PcmType::Float32LE, 1, 48000);
    whole_voice.Play(); std::array<float, 4> whole_stream{}, split_stream{};
    (void)whole_voice.Mix(whole_stream, 1, 32000);
    (void)stream_voice.Configure(std::as_bytes(std::span(continuous).first(2)), PcmType::Float32LE, 1, 48000);
    (void)stream_voice.SetNextFrame(std::as_bytes(std::span(continuous).subspan(2, 1))); stream_voice.Play();
    (void)stream_voice.Mix(std::span(split_stream).first(2), 1, 32000);
    const auto phase = stream_voice.EndPhase();
    (void)stream_voice.Configure(std::as_bytes(std::span(continuous).subspan(2)), PcmType::Float32LE, 1, 48000);
    (void)stream_voice.PlayFrom(phase); (void)stream_voice.Mix(std::span(split_stream).subspan(2), 1, 32000);
    check(phase == 1 && whole_stream == split_stream && split_stream[1] == 1.5f && split_stream[2] == 3,
          "stream_boundaries_preserve_lookahead_interpolation_and_fractional_phase");
    (void)stream_voice.Configure(std::as_bytes(std::span(sequence)), PcmType::Float32LE, 1, 48000, 1, 3, UINT32_MAX);
    const auto started = stream_voice.PlayFrom(static_cast<double>(std::uint64_t{1} << 40) + 0.5);
    const auto old_position = stream_voice.Position();
    check(started && old_position == 2.5 && !stream_voice.PlayFrom(-1) && !stream_voice.PlayFrom(std::numeric_limits<double>::infinity()) &&
          stream_voice.Position() == old_position && !stream_voice.SetNextFrame(std::span<const std::byte>(reinterpret_cast<const std::byte*>(pcm.data()), 2)),
          "stream_phase_validation_and_large_loop_phase_are_bounded_and_transactional");
    std::printf("{\"event\":\"pcm_voice_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
}
