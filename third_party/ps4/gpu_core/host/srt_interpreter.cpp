#include "cache_memory.h"
#include "shader_recompiler/ir/passes/srt.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace Shader {
namespace {
using Srt = PersistentSrtInfo;
u32 Evaluate(const Srt &plan, u32 index, std::span<const u32> flat,
             std::span<const u8> written, unsigned depth = 0) {
  if (index >= plan.offsets.size() || depth >= 128) {
    throw std::runtime_error("Invalid SRT offset graph");
  }
  const auto &node = plan.offsets[index];
  if (node.op == Srt::OffsetOp::Constant) {
    return node.a;
  }
  if (node.op == Srt::OffsetOp::Flat) {
    if (node.a >= flat.size() || !written[node.a]) {
      throw std::runtime_error("SRT offset reads an unavailable value");
    }
    return flat[node.a];
  }
  const auto a = Evaluate(plan, node.a, flat, written, depth + 1);
  if (node.op == Srt::OffsetOp::Not) {
    return ~a;
  }
  const auto b = Evaluate(plan, node.b, flat, written, depth + 1);
  switch (node.op) {
  case Srt::OffsetOp::Add:
    return a + b;
  case Srt::OffsetOp::Subtract:
    return a - b;
  case Srt::OffsetOp::Multiply:
    return a * b;
  case Srt::OffsetOp::ShiftLeft:
    return a << (b & 31);
  case Srt::OffsetOp::ShiftRight:
    return a >> (b & 31);
  case Srt::OffsetOp::And:
    return a & b;
  case Srt::OffsetOp::Or:
    return a | b;
  case Srt::OffsetOp::Xor:
    return a ^ b;
  case Srt::OffsetOp::Minimum:
    return std::min(a, b);
  case Srt::OffsetOp::Maximum:
    return std::max(a, b);
  case Srt::OffsetOp::BitExtract: {
    const auto count = Evaluate(plan, node.c, flat, written, depth + 1);
    if (b > 32 || count > 32 - b) {
      throw std::runtime_error("Undefined SRT bit-field extraction");
    }
    if (!count) {
      return 0;
    }
    return count == 32 ? a : ((a >> b) & ((u32{1} << count) - 1));
  }
  default:
    throw std::runtime_error("Unknown SRT offset operation");
  }
}
u64 Address(u64 base, u32 offset, u64 bytes) {
  constexpr u64 limit = 0x0000ffffffffffffull;
  const auto byte_offset = u64{offset} * sizeof(u32);
  if (base > limit || byte_offset > limit - base ||
      bytes - 1 > limit - base - byte_offset) {
    throw std::runtime_error(
        "SRT pointer arithmetic exceeds guest address space");
  }
  return base + byte_offset;
}
} // namespace
void PersistentSrtInfo::Execute(std::span<const u32> user_data,
                                std::span<u32> flat) const {
  if (user_data.size() > 16 || flat.size() != flattened_bufsize_dw ||
      flat.size() < user_data.size()) {
    throw std::runtime_error("Invalid flattened SRT buffer");
  }
  if (steps.empty()) {
    return;
  }
  boost::container::small_vector<u8, 128> written(flat.size(), 0);
  std::fill_n(written.begin(), user_data.size(), u8{1});
  std::array<u64, 128> stack{};
  unsigned depth{};
  u64 pointer{};
  for (std::size_t step_index = 0; step_index < steps.size(); ++step_index) {
    const auto &step = steps[step_index];
    if (step.op == StepOp::Pop) {
      if (!depth) {
        throw std::runtime_error("SRT pointer stack underflow");
      }
      pointer = stack[--depth];
      continue;
    }
    const auto offset = Evaluate(*this, step.offset, flat, written);
    if (step.op == StepOp::Read) {
      if (!depth || step.destination < 16 || step.destination >= flat.size()) {
        throw std::runtime_error("Invalid SRT read destination");
      }
      std::size_t count = 1;
      if (offsets[step.offset].op == OffsetOp::Constant) {
        while (step_index + count < steps.size() &&
               step.destination + count < flat.size() &&
               u64{offset} + count <= UINT32_MAX) {
          const auto &next = steps[step_index + count];
          if (next.op != StepOp::Read ||
              next.destination != step.destination + count ||
              next.offset >= offsets.size() ||
              offsets[next.offset].op != OffsetOp::Constant ||
              offsets[next.offset].a != offset + count) {
            break;
          }
          ++count;
        }
      }
      Ketsui::GpuCore::CacheMemory().ReadMetadata(
          Address(pointer, offset, count * sizeof(u32)),
          &flat[step.destination], count * sizeof(u32));
      std::fill_n(written.begin() + step.destination, count, u8{1});
      step_index += count - 1;
    } else if (step.op == StepOp::Root || step.op == StepOp::Push) {
      if (depth == stack.size()) {
        throw std::runtime_error("SRT pointer nesting exceeds limit");
      }
      stack[depth++] = pointer;
      if (step.op == StepOp::Root) {
        if (offset >= user_data.size() || user_data.size() - offset < 2) {
          throw std::runtime_error("SRT root outside user data");
        }
        pointer = u64{user_data[offset]} | (u64{user_data[offset + 1]} << 32);
      } else {
        Ketsui::GpuCore::CacheMemory().ReadMetadata(Address(pointer, offset, 8),
                                                    &pointer, 8);
      }
      pointer &= 0x0000ffffffffffffull;
    } else {
      throw std::runtime_error("Unknown SRT traversal operation");
    }
  }
  if (depth) {
    throw std::runtime_error("Unbalanced SRT pointer traversal");
  }
}
} // namespace Shader
