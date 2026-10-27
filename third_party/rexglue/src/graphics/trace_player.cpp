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

#include <memory>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/trace_player.h>
#include <rex/graphics/xenos.h>
#include <rex/memory.h>
#if defined(REX_GPU_TRACE_BENCH)
#endif

namespace rex::graphics {

TracePlayer::TracePlayer(GraphicsSystem* graphics_system)
    : graphics_system_(graphics_system), current_frame_index_(-1), current_command_index_(-1) {
  // Memory::Initialize already commits the entire host physical view for GPU
  // access. Reserving a guest alias of it again conflicts with the runtime's
  // writeback/kernel allocations. Replay only writes through TranslatePhysical.

  playback_event_ = rex::thread::Event::CreateAutoResetEvent(false);
  assert_not_null(playback_event_);
}

const TraceReader::Frame* TracePlayer::current_frame() const {
  if (current_frame_index_ < 0 || current_frame_index_ >= frame_count()) {
    return nullptr;
  }
  return frame(current_frame_index_);
}

void TracePlayer::SeekFrame(int target_frame) {
  if (target_frame < 0 || target_frame >= frame_count() || playing_trace_ ||
      current_frame_index_ == target_frame) {
    return;
  }
  current_frame_index_ = target_frame;
  auto frame = current_frame();
  current_command_index_ = int(frame->commands.size()) - 1;

  assert_true(frame->start_ptr <= frame->end_ptr);
  PlayTrace(frame->start_ptr, frame->end_ptr - frame->start_ptr, TracePlaybackMode::kBreakOnSwap,
            false);
}

void TracePlayer::SeekCommand(int target_command) {
  if (current_command_index_ == target_command) {
    return;
  }
  int previous_command_index = current_command_index_;
  current_command_index_ = target_command;
  if (current_command_index_ == -1) {
    return;
  }
  auto frame = current_frame();
  const auto& command = frame->commands[target_command];
  assert_true(frame->start_ptr <= command.end_ptr);
  if (previous_command_index != -1 && target_command > previous_command_index) {
    // Seek forward.
    const auto& previous_command = frame->commands[previous_command_index];
    PlayTrace(previous_command.end_ptr, command.end_ptr - previous_command.end_ptr,
              TracePlaybackMode::kBreakOnSwap, false);
  } else {
    // Full playback from frame start.
    PlayTrace(frame->start_ptr, command.end_ptr - frame->start_ptr, TracePlaybackMode::kBreakOnSwap,
              true);
  }
}

void TracePlayer::WaitOnPlayback() {
  rex::thread::Wait(playback_event_.get(), true);
}

bool TracePlayer::WaitOnPlayback(std::chrono::milliseconds timeout) {
  return rex::thread::Wait(playback_event_.get(), false, timeout) ==
         rex::thread::WaitResult::kSuccess;
}

bool TracePlayer::ReplayFrame(int target_frame) {
  if (target_frame < 0 || target_frame >= frame_count() || playing_trace_) return false;
  current_frame_index_ = target_frame;
  current_command_index_ = int(current_frame()->commands.size()) - 1;
  const auto* selected = current_frame();
  PlayTrace(selected->start_ptr, selected->end_ptr - selected->start_ptr,
            TracePlaybackMode::kUntilEnd, false);
  return true;
}

void TracePlayer::PlayTrace(const uint8_t* trace_data, size_t trace_size,
                            TracePlaybackMode playback_mode, bool clear_caches) {
  playing_trace_ = true;
  graphics_system_->command_processor()->CallInThread(
      [=]() { PlayTraceOnThread(trace_data, trace_size, playback_mode, clear_caches); });
}

void TracePlayer::PlayTraceOnThread(const uint8_t* trace_data, size_t trace_size,
                                    TracePlaybackMode playback_mode, bool clear_caches) {
  auto memory = graphics_system_->memory();
  auto command_processor = graphics_system_->command_processor();

  if (clear_caches) {
    command_processor->ClearCaches();
  }

  playback_percent_ = 0;
  auto trace_end = trace_data + trace_size;

  playing_trace_ = true;
  auto trace_ptr = trace_data;
  replay_stats_ = {};
  bool pending_break = false;
  const PacketStartCommand* pending_packet = nullptr;
  while (trace_ptr < trace_data + trace_size) {
    playback_percent_ =
        uint32_t((float(trace_ptr - trace_data) / float(trace_end - trace_data)) * 10000);

    auto type = static_cast<TraceCommandType>(memory::load<uint32_t>(trace_ptr));
    const auto command_begin = std::chrono::steady_clock::now();
    switch (type) {
      case TraceCommandType::kPrimaryBufferStart: {
        auto cmd = reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kPrimaryBufferEnd: {
        auto cmd = reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
        command_processor->TracePlaybackPrimaryBufferEnd();
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kIndirectBufferStart: {
        auto cmd = reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kPacketStart: {
        auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::memcpy(memory->TranslatePhysical(cmd->base_ptr), trace_ptr, cmd->count * 4);
        trace_ptr += cmd->count * 4;
        pending_packet = cmd;
        break;
      }
      case TraceCommandType::kPacketEnd: {
        auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (pending_packet) {
          command_processor->ExecutePacket(pending_packet->base_ptr, pending_packet->count);
          ++replay_stats_.packets;
          pending_packet = nullptr;
        }
        if (pending_break) {
          playing_trace_ = false;
          playback_event_->Set();
          return;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        auto* destination = memory->TranslatePhysical(cmd->base_ptr);
        bool reused = false;
        if (reuse_memory_) {
          auto [cached, inserted] = decoded_memory_.try_emplace(trace_ptr);
          if (inserted) {
            cached->second.resize(cmd->decoded_length);
            DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                             cached->second.data(), cmd->decoded_length);
          }
          // GPU-written pages must be restored even if their stale CPU mirror
          // equals the snapshot. Only truly unchanged CPU data may stay cached.
          reused = command_processor->TracePlaybackCanReuseMemory(cmd->base_ptr, cmd->decoded_length) &&
              !std::memcmp(destination, cached->second.data(), cmd->decoded_length);
          if (!reused) std::memcpy(destination, cached->second.data(), cmd->decoded_length);
        } else {
          DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                           destination, cmd->decoded_length);
        }
        trace_ptr += cmd->encoded_length;
        if (reused) {
          replay_stats_.reused_bytes += cmd->decoded_length;
        } else {
          command_processor->TracePlaybackWroteMemory(cmd->base_ptr, cmd->decoded_length);
          replay_stats_.restored_bytes += cmd->decoded_length;
        }
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        // ?
        // Assuming the command processor will do the same write.
        trace_ptr += cmd->encoded_length;
        break;
      }
      case TraceCommandType::kEdramSnapshot: {
        auto cmd = reinterpret_cast<const EdramSnapshotCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint8_t[]> edram_snapshot(new uint8_t[xenos::kEdramSizeBytes]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length, edram_snapshot.get(),
                         xenos::kEdramSizeBytes);
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreEdramSnapshot(edram_snapshot.get());
        replay_stats_.restored_bytes += xenos::kEdramSizeBytes;
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventCommand::Type::kSwap: {
            if (playback_mode == TracePlaybackMode::kBreakOnSwap) {
              pending_break = true;
            }
            break;
          }
        }
        break;
      }
      case TraceCommandType::kRegisters: {
        auto cmd = reinterpret_cast<const RegistersCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint32_t[]> register_values(new uint32_t[cmd->register_count]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                         register_values.get(), sizeof(uint32_t) * cmd->register_count);
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreRegisters(cmd->first_register, register_values.get(),
                                            cmd->register_count, cmd->execute_callbacks);
        break;
      }
      case TraceCommandType::kGammaRamp: {
        auto cmd = reinterpret_cast<const GammaRampCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint32_t[]> gamma_ramps(new uint32_t[256 + 3 * 128]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length, gamma_ramps.get(),
                         sizeof(uint32_t) * (256 + 3 * 128));
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreGammaRamp(
            reinterpret_cast<const reg::DC_LUT_30_COLOR*>(gamma_ramps.get()),
            reinterpret_cast<const reg::DC_LUT_PWL_DATA*>(gamma_ramps.get() + 256),
            cmd->rw_component);
        break;
      }
    }
    const double command_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - command_begin).count();
    if (type == TraceCommandType::kPacketEnd) {
      replay_stats_.packets_us += command_us;
    } else if (type == TraceCommandType::kPrimaryBufferEnd) {
      replay_stats_.primary_end_us += command_us;
    } else {
      replay_stats_.restore_us += command_us;
    }
  }

  playing_trace_ = false;

  playback_event_->Set();
}

}  // namespace rex::graphics
