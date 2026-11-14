#include "ketsui_arrange_music.h"
#include "ketsui_arrange_audio.h"
#include "common/verification_cache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
// The SDK builds this decoder but only registers the raw-frame decoder used
// by the Xbox 360 audio runtime. These files are complete XMA2 streams.
extern AVCodec ff_xma2_decoder;
}

namespace cave::ps4 {
namespace {
using Bytes = std::span<const std::uint8_t>;
struct Track {
    std::string_view name;
    std::uint64_t bytes;
    std::string_view sha256;
};
// Indexed by the PS4 game's cue numbers.
constexpr std::array kTracks{
    Track{"kkj_arr_psel.xma", 1321132, "60f59658acbde4efab3146c090d11e1f339e3564cf3061db0c0a825fbc2f95a2"},
    Track{"kkj_arr_stg1.xma", 2744576, "05b2e86212a009c5305f9cd4605a871d3a04099652d7e55da51e91a10f479833"},
    Track{"kkj_arr_stg2.xma", 3260704, "c6bbb53f0fd424756deb730a64aed32d9c0f522428d674186ed17e49d4d051f3"},
    Track{"kkj_arr_stg3.xma", 3103000, "e9958c963a3d0cc444e092a49ddeaba67b89a9344e3f4d72827063a95525b519"},
    Track{"kkj_arr_stg4.xma", 3612984, "2b62965c87420edd36063bc023afc60e7443517a7d06b84af206a0b115822884"},
    Track{"kkj_arr_stg5.xma", 5515692, "6e32b305e81d5e98b072f3d0002d2c1bd184e105a7ec3802cd64d188e68c530d"},
    Track{"kkj_arr_boss.xma", 3189020, "49e00e530ee958b6cb72146a1cb7f2a91835ac026ab6026238f452c33bf8ba23"},
    Track{"kkj_arr_lbs1.xma", 1493172, "f7ca956af2c1a1ea9e4bffee6926fa9367fc430492cfa48b85a61fdf804b6e09"},
    Track{"kkj_arr_lbs2.xma", 2224352, "e2f6c12802e34cfb55fbfd7a6d981543c8ea4fa817605c6bf669590275023afc"},
    Track{"kkj_arr_sclr.xma", 663684, "4131c04a75bd5cc3807bb1a0e9e9598c7f4c0f03d1c55c08cb463c8f12e92881"},
    Track{"kkj_arr_endg.xma", 3383592, "2221238ff22852e25cecd36667172d0b085bd5cd612bff75b4a8c3da76f59b88"},
    Track{"kkj_arr_govr.xma", 116832, "f26057f1434c55410de8585dbbb086ffbc9e01c52a15deb91edd569ed0aab0c9"},
    Track{"kkj_arr_name.xma", 1224868, "25c001d91b0ebf08e21b45a19ec8872355a98640247001ac879b5a15dc3ae257"},
};
constexpr std::string_view kDirectory = "x-arrange/";
static_assert(kTracks.size() == kKetsuiArrangeCueCount);
constexpr unsigned kPacket = 2048;
// The pinned decoder emits the XMA synthesis delay. Verified against the
// standalone XMA2 decoder on every cue from the original soundtrack.
constexpr unsigned kDecoderDelay = 576;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

fs::path TrackPath(const fs::path& data, const Track& track) {
    return DataChild(data, std::string(kDirectory) + std::string(track.name));
}

bool HasTag(Bytes bytes, std::string_view tag) {
    return bytes.size() >= tag.size() && std::equal(tag.begin(), tag.end(), bytes.begin());
}

std::uint32_t Read(Bytes bytes, unsigned offset, unsigned count = 4) {
    std::uint32_t value{};
    for (unsigned i = 0; i < count; ++i) value |= std::uint32_t(bytes[offset + i]) << (8 * i);
    return value;
}

void Append(PreparationBytes& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

void Append(PreparationBytes& bytes, std::uint32_t value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

struct Xma {
    Bytes format, packets;
    unsigned rate{}, samples{}, loop_start{}, loop_length{};
};

Xma ParseXma(Bytes bytes) {
    Require(bytes.size() >= 12 && HasTag(bytes, "RIFF") && HasTag(bytes.subspan(8), "WAVE") &&
        Read(bytes, 4) == bytes.size() - 8, "Invalid X Arrange RIFF");
    Xma result;
    for (auto chunks = bytes.subspan(12); !chunks.empty();) {
        Require(chunks.size() >= 8, "Truncated X Arrange RIFF chunk");
        const auto size = Read(chunks, 4);
        Require(size <= chunks.size() - 8 && (size + (size & 1)) <= chunks.size() - 8,
            "Invalid X Arrange RIFF chunk extent");
        if (HasTag(chunks, "fmt ")) {
            Require(result.format.empty(), "Duplicate X Arrange format");
            result.format = chunks.subspan(8, size);
        } else if (HasTag(chunks, "data")) {
            Require(result.packets.empty(), "Duplicate X Arrange data");
            result.packets = chunks.subspan(8, size);
        }
        chunks = chunks.subspan(8 + size + (size & 1));
    }
    const auto fmt = result.format;
    Require(fmt.size() == 52 && Read(fmt, 0, 2) == 0x166 && Read(fmt, 2, 2) == 2 &&
        Read(fmt, 16, 2) == 34 && Read(fmt, 18, 2) == 1 && Read(fmt, 20) == 3 &&
        Read(fmt, 32) == 0 && !result.packets.empty() && result.packets.size() % kPacket == 0,
        "Unexpected X Arrange XMA2 layout");
    result.rate = Read(fmt, 4);
    result.samples = Read(fmt, 36);
    result.loop_start = Read(fmt, 40);
    result.loop_length = Read(fmt, 44);
    const auto encoded_samples = Read(fmt, 24);
    Require(result.rate >= 47900 && result.rate <= 48100 && encoded_samples <= 11000000 &&
        result.samples > 0 && result.samples <= encoded_samples &&
        result.loop_start <= result.samples && result.loop_length <= encoded_samples - result.loop_start,
        "Invalid X Arrange sample or loop extent");
    // Most loops extend slightly past PlayLength, but stay inside the
    // encoded frames. Keep those samples so the original loop seam survives.
    result.samples = (std::max)(result.samples, result.loop_start + result.loop_length);
    return result;
}

PreparationBytes Decode(const Xma& xma) {
    const auto free_context = [](AVCodecContext* value) { avcodec_free_context(&value); };
    const auto free_packet = [](AVPacket* value) { av_packet_free(&value); };
    const auto free_frame = [](AVFrame* value) { av_frame_free(&value); };
    std::unique_ptr<AVCodecContext, decltype(free_context)> context(avcodec_alloc_context3(&ff_xma2_decoder), free_context);
    std::unique_ptr<AVPacket, decltype(free_packet)> packet(av_packet_alloc(), free_packet);
    std::unique_ptr<AVFrame, decltype(free_frame)> frame(av_frame_alloc(), free_frame);
    Require(context && packet && frame, "Cannot allocate X Arrange decoder");
    context->channels = 2;
    context->channel_layout = AV_CH_LAYOUT_STEREO;
    context->sample_rate = static_cast<int>(xma.rate);
    context->block_align = kPacket;
    const auto extra = xma.format.subspan(18);
    context->extradata = static_cast<std::uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    Require(context->extradata != nullptr, "Cannot allocate X Arrange format");
    std::ranges::copy(extra, context->extradata);
    context->extradata_size = static_cast<int>(extra.size());
    Require(avcodec_open2(context.get(), &ff_xma2_decoder, nullptr) >= 0 &&
        av_new_packet(packet.get(), kPacket) >= 0, "Cannot initialize X Arrange decoder");

    PreparationBytes wav;
    const auto pcm_bytes = xma.samples * 4;
    const auto loop_bytes = xma.loop_length ? 68u : 0u;
    wav.reserve(44 + pcm_bytes + loop_bytes);
    Append(wav, "RIFF"); Append(wav, 36 + pcm_bytes + loop_bytes);
    Append(wav, "WAVEfmt "); Append(wav, 16); Append(wav, 1, 2); Append(wav, 2, 2);
    Append(wav, xma.rate); Append(wav, xma.rate * 4); Append(wav, 4, 2); Append(wav, 16, 2);
    Append(wav, "data"); Append(wav, pcm_bytes);
    unsigned skip = kDecoderDelay, remaining = xma.samples;
    const auto drain = [&] {
        for (;;) {
            const auto status = avcodec_receive_frame(context.get(), frame.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return;
            Require(status >= 0 && frame->format == AV_SAMPLE_FMT_FLTP && frame->nb_samples > 0 &&
                frame->channels == 2 && frame->extended_data[0] && frame->extended_data[1],
                "Cannot decode X Arrange audio");
            const auto samples = static_cast<unsigned>(frame->nb_samples);
            const auto start = (std::min)(skip, samples);
            const auto count = (std::min)(remaining, samples - start);
            skip -= start;
            remaining -= count;
            const auto* left = reinterpret_cast<const float*>(frame->extended_data[0]);
            const auto* right = reinterpret_cast<const float*>(frame->extended_data[1]);
            for (unsigned i = start; i < start + count; ++i) {
                for (const auto sample : {left[i], right[i]}) {
                    const auto clipped = std::clamp(sample, -1.0f, 32767.0f / 32768.0f);
                    Append(wav, static_cast<std::uint16_t>(std::lround(clipped * 32768.0f)), 2);
                }
            }
        }
    };
    for (auto packets = xma.packets; !packets.empty(); packets = packets.subspan(kPacket)) {
        Require(av_packet_make_writable(packet.get()) >= 0, "Cannot write X Arrange packet");
        std::ranges::copy(packets.first(kPacket), packet->data);
        Require(avcodec_send_packet(context.get(), packet.get()) >= 0, "Cannot submit X Arrange packet");
        drain();
    }
    Require(avcodec_send_packet(context.get(), nullptr) >= 0, "Cannot finish X Arrange stream");
    drain();
    Require(remaining == 0 && wav.size() == 44 + pcm_bytes, "Incomplete X Arrange PCM");
    if (xma.loop_length) {
        Append(wav, "smpl"); Append(wav, 60);
        for (const auto field : {0u, 0u, 1000000000u / xma.rate, 60u, 0u, 0u, 0u, 1u, 0u,
                                 0u, 0u, xma.loop_start, xma.loop_start + xma.loop_length - 1, 0u, 0u}) {
            Append(wav, field);
        }
    }
    return wav;
}
}

nlohmann::json FindKetsuiArrangeTracks(const fs::path& data, VerificationCache& verification) {
    const auto installed = std::ranges::any_of(kTracks, [&](const auto& track) {
        return fs::exists(TrackPath(data, track));
    });
    if (!installed) return nullptr;
    auto result = nlohmann::json::array();
    for (const auto& track : kTracks) {
        const auto file = TrackPath(data, track);
        if (!fs::is_regular_file(file) || fs::file_size(file) != track.bytes ||
            verification.Digest(file) != track.sha256) {
            throw std::runtime_error("Missing or invalid X Arrange track: " + std::string(track.name));
        }
        result.push_back(track.sha256);
    }
    return result;
}

PreparationBytes DecodeKetsuiArrangeTrack(const fs::path& data, unsigned cue, std::string_view expected_hash) {
    const auto& track = kTracks.at(cue);
    const auto source = ReadPreparationInput(TrackPath(data, track), track.bytes);
    Require(source.size() == track.bytes && expected_hash == track.sha256 &&
        PreparationHash(source) == expected_hash, "X Arrange track changed during preparation");
    return Decode(ParseXma(source));
}
}
