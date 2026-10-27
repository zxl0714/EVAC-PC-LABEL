#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <atomic>
#include <string>
#include <unordered_map>

#include <rex/graphics/trace_protocol.h>
#include <rex/graphics/trace_reader.h>
#include <rex/thread.h>

namespace rex::graphics {

class GraphicsSystem;

enum class TracePlaybackMode {
  kUntilEnd,
  kBreakOnSwap,
};

class TracePlayer : public TraceReader {
 public:
  struct ReplayStats {
    double restore_us = 0, packets_us = 0, primary_end_us = 0;
    uint64_t restored_bytes = 0, reused_bytes = 0, packets = 0;
  };
  TracePlayer(GraphicsSystem* graphics_system);

  GraphicsSystem* graphics_system() const { return graphics_system_; }
  int current_frame_index() const { return current_frame_index_; }
  int current_command_index() const { return current_command_index_; }
  bool is_playing_trace() const { return playing_trace_; }
  const Frame* current_frame() const;

  // Only valid if playing_trace is true.
  // Scalar from 0-10000
  uint32_t playback_percent() const { return playback_percent_; }

  void SeekFrame(int target_frame);
  void SeekCommand(int target_command);

  void WaitOnPlayback();
  // Replay an entire captured frame even when it is already selected. Caches
  // are retained, but the trace restores captured registers/EDRAM/memory.
  bool ReplayFrame(int target_frame);
  bool WaitOnPlayback(std::chrono::milliseconds timeout);
  // Read only after WaitOnPlayback succeeds (the event publishes these values).
  const ReplayStats& replay_stats() const { return replay_stats_; }
  void set_reuse_memory(bool reuse) { reuse_memory_ = reuse; }
  bool Open(std::string_view path) {
    if (playing_trace_) return false;
    decoded_memory_.clear();
    return TraceReader::Open(path);
  }

 private:
  void PlayTrace(const uint8_t* trace_data, size_t trace_size, TracePlaybackMode playback_mode,
                 bool clear_caches);
  void PlayTraceOnThread(const uint8_t* trace_data, size_t trace_size,
                         TracePlaybackMode playback_mode, bool clear_caches);

  GraphicsSystem* graphics_system_;
  int current_frame_index_;
  int current_command_index_;
  std::atomic<bool> playing_trace_ = false;
  std::atomic<uint32_t> playback_percent_ = {0};
  std::unique_ptr<rex::thread::Event> playback_event_;
  ReplayStats replay_stats_;
  bool reuse_memory_ = false;
  std::unordered_map<const uint8_t*, std::vector<uint8_t>> decoded_memory_;
};

}  // namespace rex::graphics
