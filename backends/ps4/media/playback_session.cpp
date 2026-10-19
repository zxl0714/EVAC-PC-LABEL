#include "playback_session.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace kt::media {
namespace {
std::size_t Index(Kind kind) { return kind == Kind::Video ? 0 : 1; }
std::int64_t SteadyTime() {
    return std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
PlaybackSession::PlaybackSession(std::shared_ptr<content::ReadOnlyFile> source, NowFunction now)
    : reader_(std::move(source)), now_(now ? std::move(now) : NowFunction{SteadyTime}) {
    for (const auto& stream : reader_.Streams()) {
        auto& slot = slots_[Index(stream.kind)];
        if (slot.index != UINT32_MAX) { throw Failure(static_cast<long>(0xc00d36b4u), "Multiple tracks of one media kind"); }
        slot.index = stream.index;
    }
}
bool PlaybackSession::Enable(unsigned index) {
    for (auto& slot : slots_) {
        if (slot.index == index) {
            if (slot.enabled) { return true; }
            if (state_ != PlayState::Ready && state_ != PlayState::Stopped) { return false; }
            slot.enabled = true; return true;
        }
    }
    return false;
}
void PlaybackSession::Reset() {
    reader_.Rewind();
    for (auto& slot : slots_) { slot.eof = false; slot.candidate.reset(); slot.future.reset(); slot.token = 0; slot.end = 0; }
    offset_ = 0; started_ = now_();
}
bool PlaybackSession::Start() {
    if (state_ != PlayState::Ready && state_ != PlayState::Stopped && state_ != PlayState::Ended) { return false; }
    if (std::none_of(slots_.begin(), slots_.end(), [](const auto& slot) { return slot.enabled; })) { return false; }
    Reset(); state_ = PlayState::Playing; return true;
}
std::int64_t PlaybackSession::Position() const {
    const auto elapsed = state_ == PlayState::Playing ? (std::max)(std::int64_t{}, now_() - started_) : 0;
    return offset_ + elapsed;
}
bool PlaybackSession::Pause() {
    if (state_ == PlayState::Ended) { return true; }
    if (state_ != PlayState::Playing) { return false; }
    offset_ = Position(); state_ = PlayState::Paused; return true;
}
bool PlaybackSession::Resume() {
    if (state_ != PlayState::Paused) { return false; }
    started_ = now_(); state_ = PlayState::Playing; return true;
}
bool PlaybackSession::Stop() { Reset(); state_ = PlayState::Stopped; return true; }
std::shared_ptr<const Frame> PlaybackSession::Decode(Slot& slot) {
    if (slot.eof) { return {}; }
    auto frame = std::make_shared<Frame>(reader_.Read(slot.index));
    slot.eof = frame->end;
    if (frame->bytes.empty()) { return {}; }
    if (frame->timestamp < 0 || frame->timestamp > static_cast<std::int64_t>(reader_.Duration()) + 10000000 || frame->duration < 0) {
        throw Failure(static_cast<long>(0x80070057u), "Media packet presentation bounds");
    }
    auto duration = frame->duration;
    if (!duration && frame->stream.kind == Kind::Video) {
        duration = std::int64_t{10000000} * frame->stream.rate_denominator / frame->stream.rate_numerator;
    } else if (!duration) {
        duration = static_cast<std::int64_t>(frame->bytes.size() / (frame->stream.channels * 2)) * 10000000 / frame->stream.sample_rate;
    }
    slot.end = (std::max)(slot.end, frame->timestamp + duration);
    return frame;
}
Delivery PlaybackSession::Request(Kind kind) {
    if (state_ != PlayState::Playing) { return {}; }
    auto& slot = slots_[Index(kind)];
    if (!slot.enabled) { return {}; }
    try {
        if (kind == Kind::Audio) {
            for (unsigned count = 0; !slot.candidate && !slot.eof && count < 32; ++count) { slot.candidate = Decode(slot); }
            if (slot.candidate && !slot.token) { slot.token = next_token_++; }
            return {slot.candidate, slot.token, false};
        }
        const auto clock = Position();
        // A small worker batch lets audio decode/control commands run between
        // catch-up batches when a slow game requests video infrequently.
        for (unsigned count = 0; count < 8; ++count) {
            if (!slot.future && !slot.eof) { slot.future = Decode(slot); }
            if (!slot.future) { if (slot.eof) { break; } continue; }
            if (slot.future->timestamp > clock) { break; }
            slot.candidate = std::move(slot.future); slot.token = next_token_++;
        }
        const bool more = (!slot.future && !slot.eof) || (slot.future && slot.future->timestamp <= clock);
        return {slot.candidate, slot.token, more};
    } catch (...) { state_ = PlayState::Failed; throw; }
}
bool PlaybackSession::Commit(Kind kind, std::uint64_t token) {
    auto& slot = slots_[Index(kind)];
    if (!token || token != slot.token || !slot.candidate || (state_ != PlayState::Playing && state_ != PlayState::Paused)) { return false; }
    slot.candidate.reset(); slot.token = 0; return true;
}
PlayTransition PlaybackSession::Poll() {
    if (state_ != PlayState::Playing) { return PlayTransition::None; }
    std::int64_t end = static_cast<std::int64_t>(reader_.Duration());
    for (const auto& slot : slots_) {
        if (!slot.enabled) { continue; }
        if (!slot.eof || slot.candidate || slot.future) { return PlayTransition::None; }
        end = (std::max)(end, slot.end);
    }
    if (Position() < end) { return PlayTransition::None; }
    if (looping_) { Reset(); return PlayTransition::Looped; }
    offset_ = end; state_ = PlayState::Ended; return PlayTransition::Ended;
}
bool PlaybackSession::Drained() const {
    for (const auto& slot : slots_) {
        if (slot.enabled && (!slot.eof || slot.candidate || slot.future)) { return false; }
    }
    return state_ == PlayState::Playing;
}

bool PlaybackSessionSelfTest(std::shared_ptr<content::ReadOnlyFile> source) {
    bool passed = true; unsigned cases{};
    const auto check = [&](const char* name, bool value) {
        ++cases; passed &= value;
        std::printf("{\"event\":\"media_playback_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    std::int64_t now{};
    PlaybackSession player(std::move(source), [&] { return now; });
    check("ready_without_selected_streams_does_not_play", player.State() == PlayState::Ready && !player.Start() && !player.Request(Kind::Video).frame);
    bool selected = true;
    for (const auto& stream : player.Streams()) { selected &= player.Enable(stream.index); }
    check("select_real_streams_and_start_real_decoder", selected && !player.Enable(99) && player.Start() && player.Position() == 0);
    const auto first = player.Request(Kind::Video);
    check("first_video_frame_is_due_at_zero", first.frame && first.frame->timestamp == 0 && first.token && !first.more_due);
    const auto repeated = player.Request(Kind::Video);
    check("uncommitted_packet_is_retained_for_allocation_retry", repeated.frame == first.frame && repeated.token == first.token && !player.Commit(Kind::Video, first.token + 100));
    check("commit_consumes_once_not_by_read_attempt", player.Commit(Kind::Video, first.token) && !player.Commit(Kind::Video, first.token) && !player.Request(Kind::Video).frame);
    check("pause_prevents_new_audio_and_video_delivery", player.Pause() && !player.Request(Kind::Audio).frame && !player.Request(Kind::Video).frame);
    now += 10000000;
    check("paused_wall_time_does_not_advance_presentation", player.Position() == 0 && player.Resume() && player.Position() == 0);
    now += 2500000;
    const auto second = player.Request(Kind::Video);
    check("resume_preserves_next_frame_and_its_pts", second.frame && second.frame->timestamp == 2500000 && player.Commit(Kind::Video, second.token));
    now += 2500000;
    const auto third = player.Request(Kind::Video);
    check("last_video_frame_requires_actual_decode_and_commit", third.frame && third.frame->timestamp == 5000000 && player.Commit(Kind::Video, third.token));
    std::uint64_t audio_frames{};
    for (unsigned count = 0; count < 128; ++count) {
        const auto audio = player.Request(Kind::Audio);
        if (!audio.frame) { break; }
        audio_frames += audio.frame->bytes.size() / 4;
        if (!player.Commit(Kind::Audio, audio.token)) { passed = false; break; }
    }
    check("audio_packets_are_consumed_as_real_pcm_not_silence", audio_frames == 37888);
    check("decoded_eof_does_not_end_before_presentation_horizon", player.Poll() == PlayTransition::None && player.State() == PlayState::Playing);
    now += 5000000;
    check("natural_end_requires_real_eof_all_commits_and_clock", player.Poll() == PlayTransition::Ended && player.State() == PlayState::Ended && player.Poll() == PlayTransition::None);
    player.SetLooping(true);
    check("explicit_restart_rewinds_actual_source", player.Start() && player.Position() == 0);
    now += 20000000;
    auto late = player.Request(Kind::Video);
    while (late.more_due) { late = player.Request(Kind::Video); }
    bool loop_packets = late.frame && late.frame->timestamp == 5000000 && player.Commit(Kind::Video, late.token);
    for (unsigned count = 0; count < 128; ++count) {
        const auto audio = player.Request(Kind::Audio); if (!audio.frame) { break; }
        loop_packets &= player.Commit(Kind::Audio, audio.token);
    }
    const auto loop = player.Poll(); const auto loop_first = player.Request(Kind::Video);
    check("loop_uses_decoder_rewind_and_restarts_pts", loop_packets && loop == PlayTransition::Looped && player.Position() == 0 && loop_first.frame && loop_first.frame->timestamp == 0);
    check("stop_invalidates_pending_delivery_tokens", player.Stop() && !player.Commit(Kind::Video, loop_first.token) && !player.Request(Kind::Video).frame && player.State() == PlayState::Stopped);
    std::printf("{\"event\":\"media_playback_self_test\",\"cases\":%u,\"passed\":%s}\n", cases, passed ? "true" : "false");
    return passed;
}
}
