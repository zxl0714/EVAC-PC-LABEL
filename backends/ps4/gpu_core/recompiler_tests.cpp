#include "recompiler_tests.h"
#include "native_memory_backend.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace Ketsui::GpuCore {
namespace {
void Require(bool ok, const char *message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}
template <typename Function> bool Rejects(Function operation) {
  try {
    operation();
  } catch (const std::exception &) {
    return true;
  }
  return false;
}
void Write(void *address, const void *source, std::size_t bytes) {
  Require(CopyToNativeMemory(address, source, bytes),
          "Cannot publish authored shader descriptor table");
}
} // namespace
RecompilerResults TestRecompiler(
    const Vulkan::Instance &instance, NativeMemoryCacheBackend &memory,
    unsigned char *scratch, std::uint64_t gpu_scratch,
    const std::function<void(std::uint64_t, std::uint32_t)> &gpu_fill) {
  RecompilerResults result;
  const auto check = [&](bool ok, const char *name) {
    Require(ok, name);
    ++result.checks;
    std::printf(
        "{\"event\":\"gpu_recompiler_case\",\"name\":\"%s\",\"passed\":true}\n",
        name);
  };
  const auto child_address = gpu_scratch + 0x4000;
  const std::array<u32, 4> root{
      static_cast<u32>(child_address),
      static_cast<u32>(child_address >> 32) | 0xabcd0000u, 3, 0};
  std::array<u32, 32> first, second;
  for (u32 i = 0; i < first.size(); ++i) {
    first[i] = 0xa0000000u + i;
    second[i] = 0xb0000000u + i;
  }
  Write(scratch, root.data(), sizeof(root));
  Write(scratch + 0x4000, first.data(), sizeof(first));
  Write(scratch + 0x8000, second.data(), sizeof(second));
  std::array<u32, 16> ud{};
  ud[0] = static_cast<u32>(gpu_scratch);
  ud[1] = static_cast<u32>(gpu_scratch >> 32) | 0xcdef0000u;
  ud[2] = 3;
  const std::array<u32, 1> placeholder{0xbf810000};
  Shader::Info info{Shader::HwStage::Fragment, Shader::SwStage::Fragment,
                    Shader::ShaderParams{ud, placeholder, 1}};
  Shader::Pools pools;
  auto *block = pools.block_pool.Create(pools.inst_pool);
  Shader::IR::Program program{info};
  program.blocks.push_back(block);
  program.post_order_blocks.push_back(block);
  Shader::IR::IREmitter ir{*block};
  using namespace Shader;
  const auto root_pointer = ir.CompositeConstruct(
      ir.GetUserData(IR::ScalarReg::S0), ir.GetUserData(IR::ScalarReg::S1));
  const auto low = ir.ReadConst(root_pointer, ir.Imm32(0u));
  const auto high = ir.ReadConst(root_pointer, ir.Imm32(1u));
  const auto index = ir.ReadConst(root_pointer, ir.Imm32(2u));
  const auto child_pointer = ir.CompositeConstruct(low, high);
  const auto nested = ir.ReadConst(child_pointer, ir.IAdd(index, ir.Imm32(8u)));
  const auto duplicate =
      ir.ReadConst(child_pointer, ir.IAdd(index, ir.Imm32(8u)));
  const auto input = ir.GetUserData(IR::ScalarReg::S2);
  const std::array<IR::U32, 18> expressions{
      ir.IAdd(input, ir.Imm32(3u)),
      ir.ISub(input, ir.Imm32(1u)),
      ir.IMul(input, ir.Imm32(2u)),
      IR::U32{ir.ShiftLeftLogical(input, ir.Imm32(1u))},
      IR::U32{ir.ShiftRightLogical(input, ir.Imm32(1u))},
      ir.BitwiseAnd(input, ir.Imm32(2u)),
      ir.BitwiseOr(input, ir.Imm32(4u)),
      ir.BitwiseXor(input, ir.Imm32(1u)),
      ir.BitwiseAnd(ir.BitwiseNot(input), ir.Imm32(15u)),
      ir.IMin(input, ir.Imm32(2u), false),
      ir.IMax(input, ir.Imm32(9u), false),
      ir.BitFieldExtract(input, ir.Imm32(0u), ir.Imm32(2u), false),
      ir.IAdd(input, ir.Imm32(0xfffffffeu)),
      ir.IAdd(ir.ISub(input, ir.Imm32(5u)), ir.Imm32(4u)),
      ir.IMul(input, ir.Imm32(0xaaaaaaabu)),
      IR::U32{ir.ShiftLeftLogical(input, ir.Imm32(32u))},
      IR::U32{ir.ShiftRightLogical(input, ir.Imm32(33u))},
      ir.BitFieldExtract(input, ir.Imm32(0u), ir.Imm32(32u), false)};
  const std::array<u32, 18> expected{6, 2, 6, 6, 1, 2, 7, 2, 12,
                                     2, 9, 3, 1, 2, 1, 3, 1, 3};
  std::array<IR::U32, expressions.size()> reads;
  for (unsigned i = 0; i < reads.size(); ++i) {
    reads[i] = ir.ReadConst(child_pointer, expressions[i]);
  }
  IR::BufferInstInfo descriptor_flags{};
  descriptor_flags.sharp_source.Assign(1);
  const auto descriptor =
      ir.CompositeConstruct(low, high, ir.Imm32(32u), ir.Imm32(0u));
  const auto typed_read =
      ir.ReadConstBuffer(descriptor, ir.Imm32(5u), descriptor_flags);
  const auto metadata_before = memory.MetadataReads();
  Optimization::FlattenExtendedUserdataPass(program);
  const auto metadata_calls = memory.MetadataReads() - metadata_before;
  const auto unbatched =
      std::count_if(info.srt_info.steps.begin(), info.srt_info.steps.end(),
                    [](const auto &step) {
                      return step.op == PersistentSrtInfo::StepOp::Read ||
                             step.op == PersistentSrtInfo::StepOp::Push;
                    });
  check(metadata_calls > 0 && metadata_calls < static_cast<u64>(unbatched),
        "contiguous_descriptor_words_share_one_coherent_metadata_read");
  const auto value = [&](const IR::U32 &reference) {
    return info.flattened_ud_buf.at(reference.Inst()->Flags<u16>());
  };
  check(value(nested) == first[11],
        "nested_srt_pointer_and_memory_derived_offset_resolve");
  const auto typed_slot =
      typed_read.Inst()->Flags<IR::BufferInstInfo>().flatbuf_off_dw.Value();
  check(typed_slot >= 16 && info.flattened_ud_buf.at(typed_slot) == first[5],
        "constant_buffer_descriptor_source_preserves_its_flattened_flags");
  check(nested.Inst()->Flags<u16>() == duplicate.Inst()->Flags<u16>(),
        "descriptor_dependency_analysis_deduplicates_equal_reads");
  bool arithmetic = true;
  for (unsigned i = 0; i < reads.size(); ++i) {
    arithmetic &= value(reads[i]) == first[expected[i]];
  }
  check(arithmetic,
        "all_upstream_srt_offset_operations_preserve_u32_semantics");
  check(!info.srt_info.walker_func && !info.srt_info.walker_func_size &&
            !info.srt_info.steps.empty(),
        "srt_plan_uses_no_native_jit_or_fault_repair");
  check(info.ReadUdReg<u64>(0, 0) == (child_address | 0xabcd000000000000ull),
        "direct_descriptor_reads_use_coherent_gpu_metadata_interface");
  check(info.ReadUdReg<u32>(IR::NumScalarRegs, 2) == 3 && Rejects([&] {
          (void)info.ReadUdReg<u64>(IR::NumScalarRegs, 15);
        }) &&
            Rejects([&] { (void)info.ReadUdReg<u64>(16, 0); }),
        "direct_user_data_reads_keep_register_bounds");
  check(info.ReadUdSharp<u32>(nested.Inst()->Flags<u16>()) == first[11] &&
            Rejects([&] {
              (void)info.ReadUdSharp<u64>(
                  static_cast<u32>(info.flattened_ud_buf.size() - 1));
            }),
        "flattened_descriptor_copies_preserve_bounds_and_alignment");
  MEMORY_BASIC_INFORMATION query{};
  check(VirtualQuery(reinterpret_cast<void *>(gpu_scratch), &query,
                     sizeof(query)) &&
            query.Protect == PAGE_NOACCESS,
        "descriptor_translation_preserves_guest_cpu_noaccess");
  const auto steps = info.srt_info.steps.size();
  const auto other_address = gpu_scratch + 0x8000;
  const std::array<u32, 2> other{static_cast<u32>(other_address),
                                 static_cast<u32>(other_address >> 32)};
  Write(scratch, other.data(), sizeof(other));
  info.RefreshFlatBuf();
  check(value(nested) == second[11] && info.srt_info.steps.size() == steps,
        "same_srt_plan_tracks_rebound_nested_pointer");
  gpu_fill(reinterpret_cast<u64>(scratch + 8), 5);
  info.RefreshFlatBuf();
  check(value(nested) == second[13],
        "gpu_produced_descriptor_offset_is_read_back_before_translation");
  const auto original_ud = ud;
  ud[0] = 0;
  ud[1] = 0x39;
  check(Rejects([&] { info.RefreshFlatBuf(); }),
        "unmapped_srt_read_fails_instead_of_inventing_zero");
  ud = original_ud;
  info.RefreshFlatBuf();
  check(value(nested) == second[13],
        "descriptor_reader_recovers_after_rejected_traversal");
  auto invalid = info.srt_info;
  invalid.steps.insert(invalid.steps.begin(), {PersistentSrtInfo::StepOp::Pop});
  check(Rejects([&] { invalid.Execute(ud, info.flattened_ud_buf); }),
        "malformed_srt_pointer_stack_is_rejected");
  invalid = info.srt_info;
  invalid.steps.front().offset = UINT32_MAX;
  check(Rejects([&] { invalid.Execute(ud, info.flattened_ud_buf); }),
        "malformed_srt_expression_reference_is_rejected");
  invalid = info.srt_info;
  invalid.offsets[invalid.steps.front().offset] = {
      PersistentSrtInfo::OffsetOp::Flat,
      static_cast<u32>(info.flattened_ud_buf.size() - 1)};
  check(Rejects([&] { invalid.Execute(ud, info.flattened_ud_buf); }),
        "srt_offsets_cannot_read_uninitialized_flattened_slots");

  // Authored instruction-format fixtures, not game shader binaries.
  const std::array<u32, 7> vs{0xbeeb03ff, 7,          0x7e000280, 0x7e0202f2,
                              0xf80008cf, 0x01000000, 0xbf810000};
  const std::array<u32, 7> fs{0xbeeb03ff, 7,          0x7e0002f2, 0x7e020280,
                              0xf800180f, 0x00010100, 0xbf810000};
  const auto compile = [&](std::span<const u32> code, HwStage hw, SwStage sw) {
    std::array<u32, 16> data{};
    Info shader{hw, sw, ShaderParams{data, code, static_cast<u64>(hw) + 10}};
    RuntimeInfo runtime{};
    runtime.Initialize(hw, sw);
    if (hw == HwStage::Fragment) {
      AmdGpu::ColorBuffer buffer{};
      buffer.info.format = static_cast<u32>(AmdGpu::DataFormat::Format8_8_8_8);
      auto &color = runtime.hw.fs.color_buffers[0];
      color.data_format = buffer.GetDataFmt();
      color.num_format = buffer.GetNumberFmt();
      color.num_conversion = buffer.GetNumberConversion();
      color.swizzle = buffer.Swizzle();
      color.export_format = AmdGpu::ShaderExportFormat::ABGR_32;
    }
    Profile profile{};
    profile.supported_spirv = 0x00010600;
    profile.subgroup_size = instance.SubgroupSize();
    profile.max_viewport_width = instance.GetMaxViewportWidth();
    profile.max_viewport_height = instance.GetMaxViewportHeight();
    Pools local_pools;
    const auto translated =
        TranslateProgram(code, local_pools, shader, runtime, profile);
    Backend::Bindings bindings{};
    const auto spirv =
        Backend::SPIRV::EmitSPIRV(profile, runtime, translated, bindings);
    bool correct_model = false;
    for (std::size_t i = 5; i < spirv.size();) {
      const auto words = spirv[i] >> 16;
      if (!words || words > spirv.size() - i) {
        break;
      }
      if ((spirv[i] & 0xffff) == 15 && words >= 4) {
        correct_model = spirv[i + 1] == (hw == HwStage::Vertex ? 0u : 4u);
        break;
      }
      i += words;
    }
    Require(correct_model, "Translated shader has the wrong execution model");
    auto [status, module] = instance.GetDevice().createShaderModuleUnique(
        {.codeSize = spirv.size() * sizeof(u32), .pCode = spirv.data()});
    Require(status == vk::Result::eSuccess && module,
            "Vulkan rejected translated authored shader");
    ++result.modules;
    result.spirv_words += spirv.size();
    return spirv;
  };
  check(compile(vs, HwStage::Vertex, SwStage::Vertex)[0] == 0x07230203,
        "complete_gcn_recompiler_creates_vulkan_vertex_module");
  check(compile(fs, HwStage::Fragment, SwStage::Fragment)[0] == 0x07230203,
        "complete_gcn_recompiler_creates_vulkan_fragment_module");
  NativeMemoryRangeInfo range;
  Require(QueryNativeMemoryRange(gpu_scratch, 1, &range), "Fetch fixture has no native mapping");
  const auto fetch_address = range.mapping_address + range.mapping_bytes - 8;
  auto* fetch_cpu = scratch + (fetch_address - gpu_scratch);
  const std::array<u32, 2> fetch{0xbf8c0000u, 0xbe802000u};
  Write(fetch_cpu, fetch.data(), sizeof(fetch));
  std::array<u32, 16> fetch_ud{};
  std::memcpy(fetch_ud.data(), &fetch_address, sizeof(fetch_address));
  Info fetch_info{HwStage::Vertex, SwStage::Vertex, ShaderParams{fetch_ud, vs, 100}};
  const auto owned_fetch = Gcn::GetFetchShaderCode(fetch_info, 0);
  check(owned_fetch == std::vector<u32>(fetch.begin(), fetch.end()), "fetch_shader_returns_at_exact_mapping_end");
  const u32 wait = 0xbf8c0000u;
  Write(fetch_cpu + 4, &wait, sizeof(wait));
  check(owned_fetch.back() == 0xbe802000u, "fetch_shader_code_is_owned_not_a_guest_pointer");
  check(Rejects([&] { Gcn::GetFetchShaderCode(fetch_info, 0); }), "fetch_shader_without_return_rejects_unmapped_tail");
  check(Rejects([&] { Gcn::GetFetchShaderCode(fetch_info, 15); }), "fetch_shader_rejects_truncated_user_data_pointer");
  const u32 buffer_load = 0xe00c2000u;
  Write(fetch_cpu + 4, &buffer_load, sizeof(buffer_load));
  check(Rejects([&] { Gcn::GetFetchShaderCode(fetch_info, 0); }), "fetch_shader_rejects_truncated_two_word_instruction");
  return result;
}
} // namespace Ketsui::GpuCore
