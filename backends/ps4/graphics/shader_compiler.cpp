#include "shader_compiler.h"
#include "ketsui_gcn_host.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "shader_recompiler/recompiler.h"
#include "video_core/amdgpu/regs.h"
#include <spirv-tools/libspirv.hpp>
#include <cstring>
#include <stdexcept>
#include <list>
#include <algorithm>

namespace kt::gfx {
bool ValidateSpirv(std::span<const std::uint32_t> words, std::string* diagnostic) {
    spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_2);
    if (diagnostic) {
        diagnostic->clear();
        validator.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&, const char* message) { diagnostic->append(message); diagnostic->push_back('\n'); });
    }
    return validator.Validate(words.data(), words.size());
}
namespace {
struct RegisterRead { bool shader; std::uint32_t index, value; };
struct RegisterTrace { const pm4::RegisterBank *shader, *context; std::vector<RegisterRead> reads; };
thread_local RegisterTrace* register_trace{};
std::uint32_t Require(const pm4::RegisterBank& bank, std::size_t index) {
    const auto value = bank.Get(static_cast<std::uint32_t>(index));
    if (!value) { throw std::runtime_error("Shader configuration reads an unknown GPU register: " + std::to_string(index)); }
    if (register_trace) {
        if (&bank != register_trace->shader && &bank != register_trace->context) { throw std::runtime_error("Untracked shader register bank"); }
        register_trace->reads.push_back({&bank == register_trace->shader, static_cast<std::uint32_t>(index), *value});
    }
    return *value;
}
template<typename T> T Context(const pm4::RegisterBank& context, std::size_t byte_offset) {
    static_assert(sizeof(T) % 4 == 0);
    T result{};
    for (std::size_t index = 0; index < sizeof(T) / 4; ++index) {
        const auto value = Require(context, byte_offset / 4 - AmdGpu::Regs::ContextRegWordOffset + index);
        std::memcpy(reinterpret_cast<unsigned char*>(&result) + index * 4, &value, 4);
    }
    return result;
}

} // namespace
struct ShaderCache::Impl {
    struct MemoryRead { std::uint64_t address; std::vector<std::uint8_t> bytes; };
    struct Entry {
        ShaderStage stage; std::uint32_t binding; std::uint64_t origin, bytes;
        std::vector<std::uint32_t> code; std::vector<RegisterRead> registers; std::vector<MemoryRead> memory;
        std::shared_ptr<const CompiledShader> result;
    };
    std::list<Entry> entries;
    std::size_t maximum_entries; std::uint64_t maximum_bytes;
    ShaderCacheStats stats;
    struct Recording {
        ShaderMemoryReader reader; void* context; std::vector<MemoryRead> reads;
        std::uint64_t bytes{}; bool bounded = true;
        static bool Read(void* raw, std::uint64_t address, void* output, std::size_t bytes) {
            auto& self = *static_cast<Recording*>(raw);
            if (!self.reader || !self.reader(self.context, address, output, bytes)) { return false; }
            if (bytes > 1024 * 1024 - self.bytes || self.reads.size() >= 8192) { self.bounded = false; return true; }
            const auto* first = static_cast<const std::uint8_t*>(output);
            self.reads.push_back({address, {first, first + bytes}}); self.bytes += bytes; return true;
        }
    };
};
ShaderCache::ShaderCache(std::size_t maximum_entries, std::uint64_t maximum_bytes) : impl_(std::make_unique<Impl>()) {
    impl_->maximum_entries = maximum_entries; impl_->maximum_bytes = maximum_bytes;
}
ShaderCache::~ShaderCache() = default;
ShaderCacheStats ShaderCache::Stats() const { auto stats = impl_->stats; stats.entries = impl_->entries.size(); return stats; }
std::shared_ptr<const CompiledShader> ShaderCache::Get(ShaderStage stage, std::span<const std::uint32_t> code,
    const pm4::RegisterBank& shader, const pm4::RegisterBank& context, ShaderMemoryReader reader,
    void* reader_context, std::uint32_t first_binding, std::uint64_t guest_program_address) {
    auto& cache = *impl_;
    std::vector<std::uint8_t> snapshot;
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
        if (it->stage != stage || it->binding != first_binding || it->origin != guest_program_address ||
            !std::equal(code.begin(), code.end(), it->code.begin(), it->code.end())) { continue; }
        bool matches = true;
        for (const auto& read : it->registers) {
            const auto value = (read.shader ? shader : context).Get(read.index);
            if (!value || *value != read.value) { matches = false; break; }
        }
        if (!matches) { continue; }
        for (const auto& read : it->memory) {
            snapshot.resize(read.bytes.size());
            if (!reader || !reader(reader_context, read.address, snapshot.data(), snapshot.size())) {
                throw std::runtime_error("Cached shader dependency is no longer readable");
            }
            if (snapshot != read.bytes) { matches = false; break; }
        }
        if (!matches) { continue; }
        const auto result = it->result; cache.entries.splice(cache.entries.begin(), cache.entries, it); ++cache.stats.hits; return result;
    }
    ++cache.stats.misses;
    RegisterTrace registers{&shader, &context, {}};
    struct TraceScope {
        RegisterTrace* previous = register_trace;
        explicit TraceScope(RegisterTrace& next) { register_trace = &next; }
        ~TraceScope() { register_trace = previous; }
    } trace_scope{registers};
    Impl::Recording memory{reader, reader_context, {}, 0, true};
    auto result = std::make_shared<CompiledShader>(CompileShader(stage, code, shader, context, Impl::Recording::Read, &memory, first_binding, guest_program_address));
    const std::uint64_t bytes = memory.bytes + code.size_bytes() + registers.reads.size() * sizeof(RegisterRead) +
        4 * (result->spirv.size() + result->flat_constants.size() + result->rectangle_control.size() + result->quad_control.size() + result->rectangle_evaluation.size()) +
        result->buffers.size() * sizeof(ShaderBufferBinding) + result->images.size() * sizeof(ShaderImageBinding) + result->samplers.size() * sizeof(ShaderSamplerBinding);
    if (memory.bounded && cache.maximum_entries && bytes <= cache.maximum_bytes) {
        while (!cache.entries.empty() && (cache.entries.size() >= cache.maximum_entries || cache.stats.bytes > cache.maximum_bytes - bytes)) {
            cache.stats.bytes -= cache.entries.back().bytes; cache.entries.pop_back(); ++cache.stats.evictions;
        }
        cache.entries.push_front({stage, first_binding, guest_program_address, bytes, {code.begin(), code.end()}, std::move(registers.reads), std::move(memory.reads), result});
        cache.stats.bytes += bytes;
    }
    return result;
}
CompiledShader CompileShader(ShaderStage stage, std::span<const std::uint32_t> code,
    const pm4::RegisterBank& shader, const pm4::RegisterBank& context,
    ShaderMemoryReader reader, void* reader_context, std::uint32_t first_binding, std::uint64_t guest_program_address) {
    if (code.empty() || code.size() > 262144) { throw std::runtime_error("Shader instruction extent is invalid"); }
    if (stage != ShaderStage::Vertex && stage != ShaderStage::Pixel) { throw std::runtime_error("Shader stage is not supported"); }
    const bool vertex = stage == ShaderStage::Vertex;
    const auto base = vertex ? 0x48u : 8u;
    AmdGpu::ShaderProgram program{};
    const std::array resources{Require(shader, base + 2), Require(shader, base + 3)};
    static_assert(sizeof(program.settings) == sizeof(resources));
    std::memcpy(&program.settings, resources.data(), sizeof(resources));
    std::array<std::uint32_t, 16> user_data{};
    if (program.settings.num_user_regs > user_data.size()) { throw std::runtime_error("Shader user-data count exceeds sixteen"); }
    for (unsigned index = 0; index < program.settings.num_user_regs; ++index) { user_data[index] = Require(shader, base + 4 + index); }
    const auto hw = vertex ? Shader::HwStage::Vertex : Shader::HwStage::Fragment;
    const auto sw = vertex ? Shader::SwStage::Vertex : Shader::SwStage::Fragment;
    Shader::RuntimeInfo runtime{}; runtime.Initialize(hw, sw);
    runtime.props.fp_denorm_mode32 = program.settings.fp_denorm_mode32;
    runtime.props.fp_denorm_mode16_64 = program.settings.fp_denorm_mode64;
    runtime.props.fp_round_mode32 = program.settings.fp_round_mode32;
    runtime.props.fp_round_mode16_64 = program.settings.fp_round_mode64;
    runtime.props.num_allocated_vgprs = static_cast<unsigned char>(program.NumVgprs());
    runtime.props.num_user_data = program.settings.num_user_regs;
    runtime.props.num_input_vgprs = program.settings.vgpr_comp_cnt;
    runtime.props.dx10_clamp = program.settings.dx10_clamp;
    if (vertex) {
        const auto control = Context<AmdGpu::VsOutputControl>(context, offsetof(AmdGpu::Regs, vs_output_control));
        const auto clip = Context<AmdGpu::ClipperControl>(context, offsetof(AmdGpu::Regs, clipper_control));
        std::uint32_t raw{}; std::memcpy(&raw, &control, 4);
        if (raw || clip.user_clip_plane_enable) { throw std::runtime_error("Shader clip/misc output mapping is not implemented"); }
        runtime.hw.vs.clip_disable = clip.clip_disable;
        runtime.hw.vs.emulate_depth_negative_one_to_one = clip.clip_space == AmdGpu::ClipSpace::MinusWToW;
    } else {
        runtime.hw.fs.en_flags = Context<AmdGpu::PsInput>(context, offsetof(AmdGpu::Regs, ps_input_ena));
        runtime.hw.fs.addr_flags = Context<AmdGpu::PsInput>(context, offsetof(AmdGpu::Regs, ps_input_addr));
        if (runtime.hw.fs.en_flags.sample_coverage_ena || runtime.hw.fs.addr_flags.sample_coverage_ena) { throw std::runtime_error("Shader multisample inputs are not implemented"); }
        const auto bary = Context<AmdGpu::BarycentricControl>(context, offsetof(AmdGpu::Regs, barycentric_control));
        const auto depth = Context<AmdGpu::DepthShaderControl>(context, offsetof(AmdGpu::Regs, depth_shader_control));
        runtime.hw.fs.front_face_all_bits = bary.front_face_all_bits;
        runtime.hw.fs.depth_before_shader = depth.depth_before_shader;
        runtime.hw.fs.mrtz_mask = depth.z_export_enable | ((depth.stencil_op_val_export_enable | depth.stencil_test_val_export_enable) << 1) | (depth.mask_export_enable << 2);
        runtime.hw.fs.z_export_format = Context<AmdGpu::ShaderExportFormat>(context, offsetof(AmdGpu::Regs, z_export_format));
        // SPI_PS_IN_CONTROL follows one reserved word after PS_INPUT_ADDR.
        // Its named bitfield cannot itself be used in offsetof.
        runtime.hw.fs.num_inputs = Require(context, offsetof(AmdGpu::Regs, ps_input_addr) / 4 - AmdGpu::Regs::ContextRegWordOffset + 2) & 63;
        if (runtime.hw.fs.num_inputs > runtime.hw.fs.inputs.size()) { throw std::runtime_error("Shader interpolation count exceeds thirty-two"); }
        for (unsigned index = 0; index < runtime.hw.fs.num_inputs; ++index) {
            const auto input = Context<AmdGpu::PsInputControl>(context, offsetof(AmdGpu::Regs, ps_inputs) + index * 4);
            runtime.hw.fs.inputs[index] = {static_cast<std::uint16_t>(input.input_offset), static_cast<std::uint16_t>(input.use_default),
                static_cast<std::uint16_t>(input.flat_shade), static_cast<std::uint16_t>(input.default_value)};
        }
        const auto exports = Context<AmdGpu::ColorExportFormat>(context, offsetof(AmdGpu::Regs, color_export_format));
        for (unsigned index = 0; index < Shader::MaxColorBuffers; ++index) {
            const auto format = exports.GetFormat(index);
            if (static_cast<unsigned>(format) == 0) { continue; }
            AmdGpu::ColorBuffer buffer{};
            buffer.info = Context<AmdGpu::ColorBuffer::Color0Info>(context, offsetof(AmdGpu::Regs, color_buffers) + index * sizeof(buffer) + offsetof(AmdGpu::ColorBuffer, info));
            auto& output = runtime.hw.fs.color_buffers[index];
            output.data_format = buffer.GetDataFmt(); output.num_format = buffer.GetNumberFmt(); output.num_conversion = buffer.GetNumberConversion();
            output.export_format = format; output.swizzle = buffer.Swizzle();
        }
    }
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010500; profile.subgroup_size = 32;
    profile.max_viewport_width = 16384; profile.max_viewport_height = 16384;
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto word : code) { hash ^= word; hash *= 1099511628211ull; }
    Shader::Info info{hw, sw, Shader::ShaderParams{user_data, code, hash}};
    // The bounded instruction span is an owned host copy, not the original
    // GPU address. Relative inline-data descriptors must use the guest origin.
    info.pgm_base = guest_program_address;
    KetsuiGcnHost::MemoryScope memory{{reader, reader_context}};
    Shader::Pools pools;
    const auto ir = Shader::TranslateProgram(code, pools, info, runtime, profile);
    Shader::Backend::Bindings bindings{first_binding, first_binding};
    CompiledShader result;
    result.spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, ir, bindings);
    if (!vertex) {
        using namespace Shader::Backend::SPIRV;
        result.rectangle_control = EmitAuxilaryTessShader(AuxShaderType::RectListTCS, runtime.hw.fs);
        result.quad_control = EmitAuxilaryTessShader(AuxShaderType::QuadListTCS, runtime.hw.fs);
        result.rectangle_evaluation = EmitAuxilaryTessShader(AuxShaderType::PassthroughTES, runtime.hw.fs);
        if (!ValidateSpirv(result.rectangle_control) || !ValidateSpirv(result.quad_control) || !ValidateSpirv(result.rectangle_evaluation)) {
            throw std::runtime_error("Rectangle expansion SPIR-V validation failed");
        }
    }
    std::string validation_error;
    if (!ValidateSpirv(result.spirv, &validation_error)) { throw std::runtime_error("SPIR-V validation failed: " + validation_error); }
    for (unsigned index = 0; index < info.buffers.size(); ++index) {
        const auto& resource = info.buffers[index];
        ShaderBufferBinding buffer{}; buffer.binding = first_binding + index; buffer.writable = resource.is_written;
        if (resource.buffer_type == Shader::BufferType::Flatbuf) {
            buffer.flat_constants = true; buffer.bytes = info.flattened_ud_buf.size() * 4;
        } else if (resource.buffer_type == Shader::BufferType::Guest) {
            if (resource.post_op == Shader::SharpFetchPostOp::OffsetByProgramBase && !guest_program_address) { throw std::runtime_error("Shader inline buffer needs its original guest program address"); }
            const auto descriptor = resource.GetSharp(info);
            buffer.address = descriptor.base_address;
            buffer.bytes = descriptor.stride ? std::uint64_t{descriptor.stride} * descriptor.num_records : descriptor.num_records;
        } else { throw std::runtime_error("Shader special buffer host binding is not implemented"); }
        result.buffers.push_back(buffer);
    }
    // Resource descriptors are copied before the shader-local SRT snapshot
    // goes away. Binding numbers follow SPIR-V's unified descriptor counter.
    auto resource_binding = first_binding + static_cast<unsigned>(info.buffers.size());
    for (const auto& resource : info.images) {
        AmdGpu::Image source{};
        if (!resource.Fetch(info.flattened_ud_buf.data(), &source) || !source.Valid() || !source.Address()) {
            throw std::runtime_error("Image resource descriptor is invalid or unavailable");
        }
        const auto descriptor = resource.GetSharp(info);
        ShaderImageBinding image; image.binding = resource_binding;
        std::memcpy(image.descriptor.data(), &descriptor, sizeof(descriptor));
        image.unsupported_access = resource.is_written || resource.is_depth || resource.is_array || resource.is_atomic || resource.NumBindings(info) != 1;
        result.images.push_back(image); resource_binding += resource.NumBindings(info);
    }
    for (const auto& resource : info.samplers) {
        const auto descriptor = resource.GetSharp(info);
        ShaderSamplerBinding sampler; sampler.binding = resource_binding++;
        std::memcpy(sampler.descriptor.data(), &descriptor, sizeof(descriptor)); result.samplers.push_back(sampler);
    }
    result.flat_constants = std::move(info.flattened_ud_buf);
    result.buffer_bindings = static_cast<std::uint32_t>(info.buffers.size());
    result.image_bindings = static_cast<std::uint32_t>(info.images.size());
    result.sampler_bindings = static_cast<std::uint32_t>(info.samplers.size());
    return result;
}
}
