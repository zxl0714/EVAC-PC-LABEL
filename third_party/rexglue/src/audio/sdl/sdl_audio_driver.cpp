/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <array>
#include <cstring>
#include <bit>
#include <cmath>

#include <rex/assert.h>
#include <rex/audio/conversion.h>
#include <rex/audio/diagnostic_stats.h>
#include <rex/audio/flags.h>
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <SDL3/SDL.h>

REXCVAR_DEFINE_BOOL(audio_mute, false, "Audio", "Mute audio output");
REXCVAR_DEFINE_DOUBLE(audio_master_volume, 1.0, "Audio", "Host output gain")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::audio::sdl {

SDLAudioDriver::SDLAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

SDLAudioDriver::~SDLAudioDriver() {
  assert_true(frames_queued_.empty());
  assert_true(frames_unused_.empty());
}

bool SDLAudioDriver::Initialize() {
  // Prevent SDL from interfering with timer resolution (causes FPS drops)
  SDL_SetHintWithPriority(SDL_HINT_TIMER_RESOLUTION, "0", SDL_HINT_OVERRIDE);

  // Set audio category for proper OS audio handling
  SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");

  // Set app name for audio device identification
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "rexglue");

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXAPU_ERROR("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
    return false;
  }
  sdl_initialized_ = true;

  SDL_AudioSpec desired_spec = {};
  SDL_AudioSpec obtained_spec = {};
  desired_spec.freq = frame_frequency_;
  desired_spec.format = SDL_AUDIO_F32LE;
  desired_spec.channels = frame_channels_;
  sdl_device_channels_ = frame_channels_;
  sdl_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec,
                                          SDLCallback, this);
  if (!sdl_stream_) {
    REXAPU_ERROR("SDL_OpenAudioDeviceStream() failed: {}", SDL_GetError());
    return false;
  }

  SDL_AudioDeviceID sdl_device = SDL_GetAudioStreamDevice(sdl_stream_);
  if (!sdl_device) {
    REXAPU_ERROR("SDL_GetAudioStreamDevice() failed: {}", SDL_GetError());
    return false;
  }

  if (!SDL_GetAudioDeviceFormat(sdl_device, &obtained_spec, NULL)) {
    REXAPU_WARN("SDL_GetAudioDeviceFormat() failed: {}", SDL_GetError());
    obtained_spec = desired_spec;
  }

  if (obtained_spec.channels == 2) {
    SDL_DestroyAudioStream(sdl_stream_);
    sdl_stream_ = nullptr;
    desired_spec.channels = 2;
    sdl_device_channels_ = 2;
    sdl_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec,
                                            SDLCallback, this);
    if (!sdl_stream_) {
      REXAPU_ERROR("SDL_OpenAudioDeviceStream() stereo fallback failed: {}", SDL_GetError());
      return false;
    }
    sdl_device = SDL_GetAudioStreamDevice(sdl_stream_);
    if (!sdl_device) {
      REXAPU_ERROR("SDL_GetAudioStreamDevice() failed after stereo fallback: {}", SDL_GetError());
      return false;
    }
  }

  if (!SDL_SetAudioStreamGain(sdl_stream_, static_cast<float>(REXCVAR_GET(audio_master_volume)))) {
    REXAPU_ERROR("SDL_SetAudioStreamGain() failed: {}", SDL_GetError());
    return false;
  }
  if (!SDL_ResumeAudioDevice(sdl_device)) {
    REXAPU_ERROR("SDL_ResumeAudioDevice() failed: {}", SDL_GetError());
    return false;
  }

  if (diag::Enabled()) {
    int device_frames = 0;
    SDL_GetAudioDeviceFormat(sdl_device, &obtained_spec, &device_frames);
    REXLOG_INFO("AUDIO_DIAG init driver={} clock=steady_us input_hz={} input_channels={} input_format=f32 input_block_samples={} output_hz={} output_channels={} output_format={} device_frames={} backend={} policy=unchanged pcm_capture=false",
        reinterpret_cast<uintptr_t>(this), frame_frequency_, sdl_device_channels_, channel_samples_,
        obtained_spec.freq, obtained_spec.channels, uint32_t(obtained_spec.format), device_frames,
        SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown");
    diagnostic_running_ = true;
    diagnostic_thread_ = std::thread([this] { DiagnosticThreadMain(); });
  }

  return true;
}

void SDLAudioDriver::SubmitFrame(uint32_t frame_ptr) {
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  float* output_frame;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (frames_unused_.empty()) {
      output_frame = new float[frame_samples_];
    } else {
      output_frame = frames_unused_.top();
      frames_unused_.pop();
    }
  }

  std::memcpy(output_frame, input_frame, frame_samples_ * sizeof(float));

  static uint32_t sdl_submit_count = 0;
  if (sdl_submit_count < 10) {
    REXAPU_DEBUG("SDLAudioDriver::SubmitFrame: frame_ptr={:08X} queued_count={}", frame_ptr,
                 frames_queued_.size() + 1);
    sdl_submit_count++;
  }

  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    const auto now = diag::Enabled() ? diag::NowUs() : 0;
    frames_queued_.push({output_frame, now});
    if (now) {
      const auto before = last_submit_us_.exchange(now, std::memory_order_relaxed);
      if (before) diag::Max(submit_gap_max_us_, now - before);
      submitted_.fetch_add(1, std::memory_order_relaxed);
      diag::Max(queue_peak_, frames_queued_.size());
    }
    PROFILE_BUFFER_QUEUE_DEPTH(static_cast<int64_t>(frames_queued_.size()));
  }
}

void SDLAudioDriver::Shutdown() {
  diagnostic_running_ = false;
  if (diagnostic_thread_.joinable()) diagnostic_thread_.join();
  if (sdl_stream_) {
    SDL_DestroyAudioStream(sdl_stream_);
    sdl_stream_ = nullptr;
  }
  if (sdl_initialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdl_initialized_ = false;
  }
  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (!frames_unused_.empty()) {
    delete[] frames_unused_.top();
    frames_unused_.pop();
  }
  while (!frames_queued_.empty()) {
    delete[] frames_queued_.front().data;
    frames_queued_.pop();
  }
}

void SDLAudioDriver::SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                                 [[maybe_unused]] int total_amount) {
  SCOPE_profile_cpu_f("apu");
  if (!userdata || !stream) {
    REXAPU_ERROR("SDLAudioDriver::SDLCallback called with nullptr.");
    return;
  }
  const auto driver = static_cast<SDLAudioDriver*>(userdata);
  const auto callback_start = diag::Enabled() ? diag::NowUs() : 0;
  if (callback_start) {
    const auto previous = driver->last_callback_us_.exchange(callback_start, std::memory_order_relaxed);
    if (previous) diag::Max(driver->callback_gap_max_us_, callback_start - previous);
  }
  const int sample_count =
      static_cast<int>(channel_samples_ * std::max<uint8_t>(driver->sdl_device_channels_, 1));
  const int len = static_cast<int>(sizeof(float) * sample_count);
  float* data = SDL_stack_alloc(float, sample_count);
  if (!data) {
    REXAPU_ERROR("SDLAudioDriver::SDLCallback failed to allocate {} samples", sample_count);
    return;
  }
  while (additional_amount > 0) {
    static uint32_t sdl_callback_count = 0;
    std::unique_lock<std::mutex> guard(driver->frames_mutex_);
    if (driver->frames_queued_.empty()) {
      if (sdl_callback_count < 10) {
        REXAPU_DEBUG("SDLCallback: no frames queued (silence)");
        sdl_callback_count++;
      }
      std::memset(data, 0, len);
      if (callback_start) driver->silence_.fetch_add(1, std::memory_order_relaxed);
      if (!SDL_PutAudioStreamData(stream, data, len)) {
        REXAPU_ERROR("SDL_PutAudioStreamData() failed while filling silence: {}", SDL_GetError());
        break;
      }
      additional_amount -= len;
    } else {
      const auto queued = driver->frames_queued_.front();
      auto buffer = queued.data;
      driver->frames_queued_.pop();
      if (callback_start) {
        driver->consumed_.fetch_add(1, std::memory_order_relaxed);
        diag::Max(driver->frame_age_max_us_, diag::NowUs() - queued.submitted_us);
      }
      if (REXCVAR_GET(audio_mute)) {
        std::memset(data, 0, len);
      } else {
        switch (driver->sdl_device_channels_) {
          case 2:
            conversion::sequential_6_BE_to_interleaved_2_LE(data, buffer, channel_samples_);
            break;
          case 6:
            conversion::sequential_6_BE_to_interleaved_6_LE(data, buffer, channel_samples_);
            break;
          default:
            assert_unhandled_case(driver->sdl_device_channels_);
            break;
        }
      }
      if (callback_start) {
        float peak = 0;
        for (int i = 0; i < sample_count; ++i) {
          const float value = std::fabs(data[i]);
          if (std::isfinite(value)) peak = std::max(peak, value);
        }
        if (peak > 0.000001f) driver->signal_blocks_.fetch_add(1, std::memory_order_relaxed);
        const auto bits = std::bit_cast<uint32_t>(peak);
        auto previous = driver->signal_peak_bits_.load(std::memory_order_relaxed);
        while (bits > previous && !driver->signal_peak_bits_.compare_exchange_weak(
            previous, bits, std::memory_order_relaxed)) {}
      }
      if (!SDL_PutAudioStreamData(stream, data, len)) {
        REXAPU_ERROR("SDL_PutAudioStreamData() failed: {}", SDL_GetError());
        driver->frames_unused_.push(buffer);
        break;
      }
      driver->frames_unused_.push(buffer);

      auto ret = driver->semaphore_->Release(1, nullptr);
      assert_true(ret);
      additional_amount -= len;
    }
  }
  SDL_stack_free(data);
  if (callback_start) diag::Max(driver->callback_busy_max_us_, diag::NowUs() - callback_start);
}

void SDLAudioDriver::DiagnosticThreadMain() {
  uint64_t last_report = diag::NowUs();
  uint64_t prev_submitted = 0, prev_consumed = 0, prev_silence = 0, prev_signal = 0;
  uint64_t prev_guest = 0, prev_guest_busy = 0, prev_xma = 0, prev_xma_busy = 0;
  double sdl_peak_ms = 0;
  while (diagnostic_running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!diagnostic_running_.load()) break;
    const auto now = diag::NowUs();
    size_t depth;
    uint64_t oldest_us = 0;
    {
      std::lock_guard lock(frames_mutex_);
      depth = frames_queued_.size();
      if (depth && frames_queued_.front().submitted_us && now >= frames_queued_.front().submitted_us)
        oldest_us = now - frames_queued_.front().submitted_us;
    }
    // Query separately: never hold frames_mutex_ while taking SDL's stream lock.
    const int queued_bytes = SDL_GetAudioStreamQueued(sdl_stream_);
    const int available_bytes = SDL_GetAudioStreamAvailable(sdl_stream_);
    const double sdl_ms = queued_bytes < 0 ? -1.0 :
        queued_bytes * 1000.0 / (frame_frequency_ * sdl_device_channels_ * sizeof(float));
    sdl_peak_ms = std::max(sdl_peak_ms, sdl_ms);
    if (now - last_report < 1000000) continue;
    const double interval_ms = (now - last_report) / 1000.0;
    const auto submitted = submitted_.load(), consumed = consumed_.load(), silence = silence_.load();
    const auto last_submit = last_submit_us_.load(), last_callback = last_callback_us_.load();
    const auto age = frame_age_max_us_.exchange(0), submit_gap = submit_gap_max_us_.exchange(0);
    const auto callback_gap = callback_gap_max_us_.exchange(0), callback_busy = callback_busy_max_us_.exchange(0);
    const auto peak = queue_peak_.exchange(depth);
    const auto signal = signal_blocks_.load();
    const float sample_peak = std::bit_cast<float>(signal_peak_bits_.exchange(0));
    REXLOG_INFO("AUDIO_DIAG signal t_us={} driver={} non_silent_blocks={} peak={:.6f} muted={} pcm_capture=false",
        now, reinterpret_cast<uintptr_t>(this), signal-prev_signal, sample_peak, REXCVAR_GET(audio_mute));
    prev_signal = signal;
    REXLOG_INFO("AUDIO_DIAG stream t_us={} driver={} interval_ms={:.1f} submitted={} consumed={} silence_blocks={} depth={} depth_peak={} queue_ms={:.1f} oldest_ms={:.1f} consumed_age_max_ms={:.1f} sdl_input_bytes={} sdl_input_ms={:.1f} sdl_input_peak_ms={:.1f} sdl_available_bytes={} submit_gap_max_ms={:.1f} submit_idle_ms={:.1f} callback_gap_max_ms={:.1f} callback_idle_ms={:.1f} callback_busy_max_ms={:.1f}",
        now, reinterpret_cast<uintptr_t>(this), interval_ms, submitted-prev_submitted,
        consumed-prev_consumed, silence-prev_silence, depth, peak,
        depth*channel_samples_*1000.0/frame_frequency_, oldest_us/1000.0, age/1000.0,
        queued_bytes, sdl_ms, sdl_peak_ms, available_bytes, submit_gap/1000.0,
        last_submit ? (now>=last_submit ? (now-last_submit)/1000.0 : 0.0) : -1.0, callback_gap/1000.0,
        last_callback ? (now>=last_callback ? (now-last_callback)/1000.0 : 0.0) : -1.0, callback_busy/1000.0);
    auto& c = diag::counters;
    const auto guest = c.guest_callbacks.load(), guest_busy = c.guest_busy_us.load();
    const auto xma = c.xma_work.load(), xma_busy = c.xma_busy_us.load();
    const auto guest_start = c.guest_started_us.load(), guest_end = c.guest_finished_us.load();
    const auto xma_start = c.xma_started_us.load(), xma_active = c.xma_active.load();
    REXLOG_INFO("AUDIO_DIAG upstream t_us={} guest_callbacks={} guest_busy_ms={:.1f} guest_max_ms={:.1f} guest_inflight_ms={:.1f} guest_callback={:08X} xma_work={} xma_busy_ms={:.1f} xma_max_ms={:.1f} xma_active={} xma_activity_span_ms={:.1f} xma_last_context={} xma_slow_total={} xma_resets_total={} xma_releases_total={} xma_kicks_total={}",
        now, guest-prev_guest, (guest_busy-prev_guest_busy)/1000.0,
        c.guest_max_us.exchange(0)/1000.0,
        guest_start>guest_end && now>=guest_start ? (now-guest_start)/1000.0 : 0.0,
        uint32_t(c.guest_callback.load()), xma-prev_xma, (xma_busy-prev_xma_busy)/1000.0,
        c.xma_max_us.exchange(0)/1000.0,
        xma_active, xma_active && now>=xma_start ? (now-xma_start)/1000.0 : 0.0,
        c.xma_context.load(), c.xma_slow.load(), c.xma_resets.load(),
        c.xma_releases.load(), c.xma_kicks.load());
    prev_submitted=submitted; prev_consumed=consumed; prev_silence=silence;
    prev_guest=guest; prev_guest_busy=guest_busy; prev_xma=xma; prev_xma_busy=xma_busy;
    sdl_peak_ms=0; last_report=now;
  }
}

}  // namespace rex::audio::sdl
