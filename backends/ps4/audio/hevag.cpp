#include "hevag.h"
#include "hevag_coefficients.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace kt::audio {
namespace {
unsigned Byte(std::byte value) { return std::to_integer<unsigned>(value); }
unsigned Big32(std::span<const std::byte> data, unsigned at) {
    return Byte(data[at]) << 24 | Byte(data[at + 1]) << 16 | Byte(data[at + 2]) << 8 | Byte(data[at + 3]);
}
}
VagInfo ParseVag(std::span<const std::byte> file) {
    if (file.size() < 48 || file.size() > 16 * 1024 * 1024) { throw VagFailure{"VAG size"}; }
    if (Big32(file, 0) != 0x56414770) { throw VagFailure{"VAG signature", true}; }
    const auto version = Big32(file, 4);
    if (version != 0x20001 && version != 0x30000) { throw VagFailure{"VAG version", true}; }
    const auto channels = Byte(file[30]) ? Byte(file[30]) : 1;
    if (channels > 2 || Big32(file, 8) || Big32(file, 20) || Big32(file, 24) || file[28] != std::byte{} ||
        file[29] != std::byte{} || file[31] != std::byte{}) { throw VagFailure{"VAG header profile", true}; }
    const auto bytes = Big32(file, 12), rate = Big32(file, 16);
    if (!bytes || bytes != file.size() - 48 || bytes % (16 * channels) || rate < 8000 || rate > 192000) {
        throw VagFailure{"VAG extent/rate"};
    }
    for (unsigned at = 48; at < file.size(); at += 16) {
        const auto predictor = (Byte(file[at]) >> 4) | (Byte(file[at + 1]) & 0xf0);
        const auto flags = Byte(file[at + 1]) & 15;
        if (predictor >= 128 || flags > 7) { throw VagFailure{"VAG frame header"}; }
        if (flags == 3 || flags == 6) { throw VagFailure{"VAG embedded loop", true}; }
    }
    return {channels, rate, 48, bytes, bytes / (16 * channels) * 28};
}
bool DecodeHevagFrame(std::span<const std::byte> frame, std::array<std::int32_t, 4>& history,
                     std::span<std::int16_t> output) {
    if (frame.size() != 16 || output.size() != 28) { return false; }
    const auto index = (Byte(frame[0]) >> 4) | (Byte(frame[1]) & 0xf0);
    const auto shift = Byte(frame[0]) & 15, flags = Byte(frame[1]) & 15;
    if (index >= 128 || flags > 7) { return false; }
    auto next = history;
    std::array<std::int16_t, 28> decoded{};
    for (unsigned sample = 0; sample < 28; ++sample) {
        std::int32_t value{};
        if (flags < 7) {
            const auto packed = Byte(frame[2 + sample / 2]);
            const auto raw = (sample & 1 ? packed >> 4 : packed) & 15;
            const auto nibble = raw < 8 ? static_cast<int>(raw) : static_cast<int>(raw) - 16;
            const auto unscaled = nibble * 4096, scale = 1 << shift;
            // Define arithmetic right shift without shifting a negative value.
            const auto code = unscaled < 0 ? -((-unscaled + scale - 1) / scale) : unscaled / scale;
            const auto* coefficients = kHevagCoefficients[index];
            const float prediction = next[0] * coefficients[0] + next[1] * coefficients[1] +
                                     next[2] * coefficients[2] + next[3] * coefficients[3];
            if (!std::isfinite(prediction) || prediction < -2147483648.0f || prediction >= 2147483648.0f) { return false; }
            const auto wide = static_cast<std::int64_t>(static_cast<std::int32_t>(prediction)) + code;
            if (wide < INT32_MIN || wide > INT32_MAX) { return false; }
            value = static_cast<std::int32_t>(wide);
        }
        decoded[sample] = static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
        next = {value, next[0], next[1], next[2]};
    }
    std::copy(decoded.begin(), decoded.end(), output.begin()); history = next;
    return true;
}
std::vector<std::byte> DecodeHevag(std::span<const std::byte> frames, unsigned channels) {
    if (!channels || channels > 2 || frames.empty() || frames.size() > 16 * 1024 * 1024 || frames.size() % (channels * 16)) {
        throw VagFailure{"HEVAG block extent"};
    }
    const auto count = frames.size() / (channels * 16);
    std::vector<std::byte> result(count * 28 * channels * 2);
    std::array<std::array<std::int32_t, 4>, 2> histories{};
    for (std::size_t group = 0; group < count; ++group) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            std::array<std::int16_t, 28> pcm{};
            if (!DecodeHevagFrame(frames.subspan((group * channels + channel) * 16, 16), histories[channel], pcm)) {
                throw VagFailure{"HEVAG decode"};
            }
            for (unsigned sample = 0; sample < 28; ++sample) {
                const auto at = ((group * 28 + sample) * channels + channel) * 2;
                std::memcpy(result.data() + at, &pcm[sample], 2);
            }
        }
    }
    return result;
}
bool HevagSelfTest() {
    bool passed = true; unsigned count{};
    const auto check = [&](const char* name, bool value) {
        ++count; passed &= value;
        std::printf("{\"event\":\"hevag_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    std::array<std::byte, 16> frame{}; std::array<std::int32_t, 4> history{};
    std::array<std::int16_t, 28> pcm{};
    check("zero_frame", DecodeHevagFrame(frame, history, pcm) && std::all_of(pcm.begin(), pcm.end(), [](auto x) { return x == 0; }));
    frame[0] = std::byte{12}; std::fill(frame.begin() + 2, frame.end(), std::byte{0x81});
    check("signed_nibbles_low_first", DecodeHevagFrame(frame, history, pcm) && pcm[0] == 1 && pcm[1] == -8 && pcm[27] == -8);
    frame.fill(std::byte{}); frame[0] = std::byte{0xdc}; frame[1] = std::byte{0x10}; history = {8, 4, 2, 1};
    check("four_history_predictor_and_truncation", DecodeHevagFrame(frame, history, pcm) && pcm[0] == 1 && pcm[1] == -7);
    frame[1] = std::byte{0x17}; history = {8, 4, 2, 1};
    check("flag_seven_is_silence_and_clears_history", DecodeHevagFrame(frame, history, pcm) && pcm[0] == 0 && history == std::array<std::int32_t, 4>{});
    frame[1] = std::byte{0xf0}; pcm.fill(73); history = {1, 2, 3, 4}; const auto before = history;
    check("bad_predictor_never_changes_output_or_history", !DecodeHevagFrame(frame, history, pcm) && history == before && pcm[0] == 73 && pcm[27] == 73);
    frame.fill(std::byte{}); frame[0] = std::byte{12}; std::fill(frame.begin() + 2, frame.end(), std::byte{0x11});
    std::array<std::byte, 32> stereo{}; std::copy(frame.begin(), frame.end(), stereo.begin());
    std::fill(frame.begin() + 2, frame.end(), std::byte{0xff}); std::copy(frame.begin(), frame.end(), stereo.begin() + 16);
    const auto decoded = DecodeHevag(stereo, 2); std::int16_t left{}, right{};
    std::memcpy(&left, decoded.data(), 2); std::memcpy(&right, decoded.data() + 2, 2);
    check("stereo_frame_interleave", decoded.size() == 112 && left == 1 && right == -1);
    std::array<std::byte, 80> file{};
    const auto put = [&](unsigned at, unsigned value) { for (unsigned n = 0; n < 4; ++n) { file[at + n] = static_cast<std::byte>(value >> (24 - 8 * n)); } };
    put(0, 0x56414770); put(4, 0x20001); put(12, 32); put(16, 48000); file[30] = std::byte{1};
    const auto info = ParseVag(file);
    check("vag_big_endian_header_and_sample_count", info.channels == 1 && info.rate == 48000 && info.data_offset == 48 && info.data_bytes == 32 && info.samples == 56);
    bool malformed{}; put(12, 31); try { ParseVag(file); } catch (const VagFailure& error) { malformed = !error.unsupported; }
    check("truncated_or_unaligned_vag_is_not_a_waveform", malformed);
    bool loops{}; put(12, 32); file[49] = std::byte{6}; try { ParseVag(file); } catch (const VagFailure& error) { loops = error.unsupported; }
    check("unimplemented_embedded_loop_is_explicit", loops);
    bool rejected{}; try { DecodeHevag(std::span<const std::byte>{file}.first(15), 1); } catch (const VagFailure&) { rejected = true; }
    check("partial_adpcm_frame_is_rejected", rejected);
    std::printf("{\"event\":\"hevag_self_test\",\"cases\":%u,\"passed\":%s}\n", count, passed ? "true" : "false");
    return passed;
}
}
