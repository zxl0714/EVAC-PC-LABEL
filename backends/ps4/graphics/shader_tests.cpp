#include "shader_binary.h"
#include "vulkan_device.h"
#include "texture.h"
#include "test_programs.h"
#include <sirit/sirit.h>
#include "video_core/amdgpu/regs.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace kt::gfx {
namespace {
struct Memory {
    static constexpr std::uint64_t base = 0x100000;
    std::vector<unsigned char> bytes = std::vector<unsigned char>(512);
    unsigned reads{};
    bool fail{};
    void Word(std::size_t offset, std::uint32_t word) { std::memcpy(bytes.data() + offset, &word, 4); }
    static bool Read(void* raw, std::uint64_t address, void* output, std::size_t length) {
        auto& self = *static_cast<Memory*>(raw); ++self.reads;
        if (self.fail || address < base || address - base > self.bytes.size() || length > self.bytes.size() - (address - base)) { return false; }
        std::memcpy(output, self.bytes.data() + (address - base), length); return true;
    }
};
// Authored GCN: constant (0,0,0,1) position, and a distinct red fragment.
// These are instruction-format fixtures, not copied game programs.
constexpr std::array<std::uint32_t, 7> vertex{0xbeeb03ffu, 7u, 0x7e000280u, 0x7e0202f2u, 0xf80008cfu, 0x01000000u, 0xbf810000u};
constexpr std::array<std::uint32_t, 7> pixel{0xbeeb03ffu, 7u, 0x7e0002f2u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u};
Memory Blob(bool is_vertex = true) {
    Memory result;
    const auto& code = is_vertex ? vertex : pixel;
    for (unsigned index = 0; index < code.size(); ++index) { result.Word(index * 4, code[index]); }
    std::memcpy(result.bytes.data() + 64, "OrbShdr\7", 8);
    result.Word(72, (static_cast<std::uint32_t>(code.size() * 4) << 8) | (is_vertex ? 5u : 1u));
    result.Word(80, 0x12345678); result.Word(84, 0x9abcdef0);
    return result;
}
template<typename Function> bool Rejects(Function&& function) {
    try { function(); return false; } catch (const std::exception&) { return true; }
}
void Set(pm4::RegisterBank& bank, std::size_t index, std::uint32_t value) { bank.values.at(index) = value; bank.known.set(index); }
void ContextWord(pm4::RegisterBank& context, std::size_t byte_offset, std::uint32_t value) {
    Set(context, byte_offset / 4 - AmdGpu::Regs::ContextRegWordOffset, value);
}
bool EntryPoint(std::span<const std::uint32_t> code, std::uint32_t model) {
    for (std::size_t index = 5; index < code.size();) {
        const auto words = code[index] >> 16; if (!words || words > code.size() - index) { return false; }
        if ((code[index] & 65535) == 15 && words >= 4) { return code[index + 1] == model; }
        index += words;
    }
    return false;
}
std::vector<std::uint32_t> SamplePixel(float u, float v, bool discard = false) {
    Sirit::Module module{0x00010500}; module.AddCapability(spv::Capability::Shader);
    if (discard) { module.AddExtension("SPV_EXT_demote_to_helper_invocation"); module.AddCapability(spv::Capability::DemoteToHelperInvocation); }
    module.SetMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    const auto f32 = module.TypeFloat(32), vec2 = module.TypeVector(f32, 2), vec4 = module.TypeVector(f32, 4);
    const auto image_type = module.TypeImage(f32, spv::Dim::Dim2D, 0, false, false, 1, spv::ImageFormat::Unknown);
    const auto sampler_type = module.TypeSampler();
    const auto image = module.AddGlobalVariable(module.TypePointer(spv::StorageClass::UniformConstant, image_type), spv::StorageClass::UniformConstant);
    const auto sampler = module.AddGlobalVariable(module.TypePointer(spv::StorageClass::UniformConstant, sampler_type), spv::StorageClass::UniformConstant);
    module.Decorate(image, spv::Decoration::DescriptorSet, 0); module.Decorate(image, spv::Decoration::Binding, 0);
    module.Decorate(sampler, spv::Decoration::DescriptorSet, 0); module.Decorate(sampler, spv::Decoration::Binding, 1);
    const auto output = module.AddGlobalVariable(module.TypePointer(spv::StorageClass::Output, vec4), spv::StorageClass::Output);
    module.Decorate(output, spv::Decoration::Location, 0);
    const auto entry = module.OpFunction(module.TypeVoid(), spv::FunctionControlMask::MaskNone, module.TypeFunction(module.TypeVoid()));
    module.AddEntryPoint(spv::ExecutionModel::Fragment, entry, "main", image, sampler, output);
    module.AddExecutionMode(entry, spv::ExecutionMode::OriginUpperLeft);
    module.AddLabel(module.OpLabel());
    if (discard) { module.OpDemoteToHelperInvocationEXT(); }
    const auto combined = module.OpSampledImage(module.TypeSampledImage(image_type), module.OpLoad(image_type, image), module.OpLoad(sampler_type, sampler));
    const auto texel = module.OpImageSampleExplicitLod(vec4, combined, module.OpCompositeConstruct(vec2, module.Constant(f32, u), module.Constant(f32, v)),
        spv::ImageOperandsMask::Lod, module.Constant(f32, 0.0f));
    module.OpStore(output, texel); module.OpReturn(); module.OpFunctionEnd(); return module.Assemble();
}
}
bool ShaderSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        std::printf("{\"event\":\"shader_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false"); passed &= value;
    };
    auto memory = Blob(); const auto original = memory.bytes;
    const auto binary = ReadShaderBinary(Memory::Read, &memory, Memory::base);
    check("declared_vertex_length_stage_and_hash_are_read_without_end_token_scanning", binary.stage == ShaderStage::Vertex &&
          binary.instructions == std::vector(vertex.begin(), vertex.end()) && binary.metadata_offset == 64 && binary.declared_hash == 0x9abcdef012345678ull && memory.reads == 3 && memory.bytes == original);
    auto fragment = Blob(false);
    check("pixel_metadata_identifies_its_own_stage", ReadShaderBinary(Memory::Read, &fragment, Memory::base).stage == ShaderStage::Pixel);
    check("invalid_address_alignment_and_overflow_are_rejected_before_read", Rejects([&] { ReadShaderBinary(Memory::Read, &memory, 1); }) &&
          Rejects([&] { ReadShaderBinary(Memory::Read, &memory, UINT64_MAX - 255); }) && memory.reads == 3);
    auto bad = Blob(); bad.Word(0, 0);
    check("missing_entry_declaration_is_rejected", Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); }) && bad.reads == 1);
    bad = Blob(); bad.Word(4, UINT32_MAX);
    check("metadata_window_is_bounded_before_pointer_following", Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); }) && bad.reads == 1);
    bad = Blob(); bad.bytes[64] = 0;
    const bool bad_signature = Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); });
    bad = Blob(); bad.bytes[71] = 6;
    check("signature_and_version_are_both_checked", bad_signature && Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); }));
    bad = Blob(); bad.bytes.resize(80);
    check("truncated_metadata_fails_without_overread", Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); }));
    bool invalid_lengths = true;
    for (const unsigned length : {0u, 3u, 68u}) {
        bad = Blob(); bad.Word(72, (length << 8) | 5u);
        invalid_lengths &= Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); });
    }
    check("zero_unaligned_and_metadata_overlapping_code_lengths_rejected", invalid_lengths);
    bad = Blob(); bad.Word(72, (28u << 8) | 9u);
    check("unsupported_stage_is_not_relabelled_vertex_or_pixel", Rejects([&] { ReadShaderBinary(Memory::Read, &bad, Memory::base); }));
    pm4::RegisterBank shader, context;
    context.known.set(); // Deliberate all-zero configuration of this authored fixture.
    Set(shader, 0x4a, 0); Set(shader, 0x4b, 0); Set(shader, 10, 0); Set(shader, 11, 0);
    ContextWord(context, offsetof(AmdGpu::Regs, color_export_format), 9);
    ContextWord(context, offsetof(AmdGpu::Regs, color_buffers) + offsetof(AmdGpu::ColorBuffer, info), static_cast<unsigned>(AmdGpu::DataFormat::Format8_8_8_8) << 2);
    const auto vs = CompileShader(ShaderStage::Vertex, vertex, shader, context, nullptr, nullptr);
    check("authored_vertex_converts_to_valid_vulkan_1_2_spirv_1_5", ValidateSpirv(vs.spirv) && vs.spirv[0] == 0x07230203 && vs.spirv[1] == 0x00010500 && EntryPoint(vs.spirv, 0) && vs.buffers.empty());
    const auto ps = CompileShader(ShaderStage::Pixel, pixel, shader, context, nullptr, nullptr);
    check("authored_fragment_converts_with_correct_execution_model", ValidateSpirv(ps.spirv) && EntryPoint(ps.spirv, 4) && ps.buffers.empty());
    check("conversion_is_deterministic_for_identical_code_and_registers", CompileShader(ShaderStage::Vertex, vertex, shader, context, nullptr, nullptr).spirv == vs.spirv);
    std::string diagnostic;
    auto corrupt = vs.spirv; corrupt[0] = 0;
    check("official_validator_rejects_corrupt_output_and_returns_diagnostic", !ValidateSpirv(corrupt, &diagnostic) && !diagnostic.empty());
    check("unknown_registers_are_not_substituted_with_zero", Rejects([&] { CompileShader(ShaderStage::Vertex, vertex, pm4::RegisterBank{}, context, nullptr, nullptr); }) &&
          Rejects([&] { CompileShader(ShaderStage::Vertex, vertex, shader, pm4::RegisterBank{}, nullptr, nullptr); }));
    const std::array<std::uint32_t, 1> short_literal{0xbeeb03ff}, short_64{0xd0000000};
    check("truncated_literal_and_sixty_four_bit_instructions_fail_safely", Rejects([&] { CompileShader(ShaderStage::Vertex, short_literal, shader, context, nullptr, nullptr); }) &&
          Rejects([&] { CompileShader(ShaderStage::Vertex, short_64, shader, context, nullptr, nullptr); }));
    // Authored direct scalar read, using the callback rather than a host pointer.
    const std::array<std::uint32_t, 9> scalar{0xbeeb03ffu, 7u, 0xc0800100u, 0xbf8c007fu, 0x7e000200u, 0x7e0202f2u, 0xf800180fu, 0x01000000u, 0xbf810000u};
    Set(shader, 11, 4); Set(shader, 12, static_cast<std::uint32_t>(Memory::base + 256)); Set(shader, 13, 0);
    memory.Word(256, 0x3f000000); memory.Word(260, 0); memory.Word(264, 0); memory.Word(268, 0);
    memory.reads = 0;
    const auto loaded = CompileShader(ShaderStage::Pixel, scalar, shader, context, Memory::Read, &memory, 5);
    check("scalar_constant_reads_use_host_and_preserve_flat_binding", memory.reads == 4 && loaded.buffers.size() == 1 && loaded.buffers[0].flat_constants &&
          loaded.buffers[0].binding == 5 && loaded.flat_constants.size() == 20 && loaded.flat_constants[16] == 0x3f000000 && ValidateSpirv(loaded.spirv));
    memory.fail = true;
    check("failed_constant_read_aborts_translation_instead_of_inventing_zero", Rejects([&] { CompileShader(ShaderStage::Pixel, scalar, shader, context, Memory::Read, &memory); }));
    check("memory_reader_scope_is_restored_after_failure", CompileShader(ShaderStage::Vertex, vertex, shader, context, nullptr, nullptr).spirv == vs.spirv);
    const auto blend = DecodeGcnBlend(0x65010504);
    check("gcn_separate_alpha_blend_uses_independent_factors", blend.enabled && blend.source_color == BlendFactor::SourceAlpha &&
        blend.destination_color == BlendFactor::InverseSourceAlpha && blend.source_alpha == BlendFactor::One &&
        blend.destination_alpha == BlendFactor::InverseSourceAlpha && blend.color == BlendOperation::Add && blend.alpha == BlendOperation::Add);
    const auto shared = DecodeGcnBlend(0x40000101);
    check("gcn_shared_alpha_reuses_color_equation", shared.source_color == shared.source_alpha && shared.destination_color == shared.destination_alpha && shared.color == shared.alpha);
    const auto constant = DecodeGcnBlend(0x74130e0d);
    check("gcn_constant_color_alpha_factors_keep_their_distinct_encoding", constant.source_color == BlendFactor::ConstantColor &&
        constant.destination_color == BlendFactor::InverseConstantColor && constant.source_alpha == BlendFactor::ConstantAlpha && constant.destination_alpha == BlendFactor::InverseConstantAlpha);
    check("reserved_and_dual_source_blend_modes_are_explicit", Rejects([] { DecodeGcnBlend(0x80000000); }) &&
        Rejects([] { DecodeGcnBlend(0x4000000f); }) && Rejects([] { DecodeGcnBlend(0x400000a0); }));
    auto fetch_memory = Memory{};
    // Authored S_SWAPPC to a two-instruction routine: v0 = 0.5; return s[0:1].
    // Inlining changes the shader result, rather than treating fetch as a NOP.
    const std::array<std::uint32_t, 7> fetched_vertex{0xbeeb03ffu, 7u, 0xbe802104u, 0x7e0202f2u, 0xf80008cfu, 0x01000000u, 0xbf810000u};
    fetch_memory.Word(0, 0x7e0002f0); fetch_memory.Word(4, 0xbe802000);
    Set(shader, 0x4b, 6u << 1);
    for (unsigned index = 0; index < 6; ++index) { Set(shader, 0x4c + index, 0); }
    Set(shader, 0x50, Memory::base);
    const auto fetched = CompileShader(ShaderStage::Vertex, fetched_vertex, shader, context, Memory::Read, &fetch_memory);
    check("fetch_inlines_bounded_guest_instructions_through_reader", ValidateSpirv(fetched.spirv) && fetched.spirv != vs.spirv && fetch_memory.reads == 2 && fetched.buffers.empty());
    fetch_memory.Word(4, 0xbe802002);
    check("fetch_rejects_a_return_to_the_wrong_link_register", Rejects([&] { CompileShader(ShaderStage::Vertex, fetched_vertex, shader, context, Memory::Read, &fetch_memory); }));
    fetch_memory.bytes.resize(4);
    check("truncated_fetch_stops_at_reader_boundary", Rejects([&] { CompileShader(ShaderStage::Vertex, fetched_vertex, shader, context, Memory::Read, &fetch_memory); }));
    fetch_memory.bytes.resize(4096);
    for (unsigned index = 0; index < 4096; index += 4) { fetch_memory.Word(index, 0xbf8c007f); }
    fetch_memory.reads = 0;
    check("unterminated_fetch_is_bounded_to_4096_bytes", Rejects([&] { CompileShader(ShaderStage::Vertex, fetched_vertex, shader, context, Memory::Read, &fetch_memory); }) && fetch_memory.reads == 1024);
    std::array<std::uint32_t, 8> texture_words{0x1000, 10u << 20, 15u | (8u << 14), 0x90000000u | (13u << 20) | 0xfacu, 15u << 13, 0, 0, 0};
    const auto texture_layout = DescribeTexture(texture_words);
    check("rgba8_single_mip_texture_descriptor_preserves_extent_and_swizzle", texture_layout.address == 0x100000 && texture_layout.width == 16 && texture_layout.height == 9 &&
        texture_layout.pitch == 16 && texture_layout.padded_height == 16 && texture_layout.bytes == 1024 && texture_layout.components == std::array<std::uint32_t, 4>{4, 5, 6, 7});
    std::array<bool, 256> seen{}; bool bijective = true;
    for (unsigned y = 0; y < 16; ++y) { for (unsigned x = 0; x < 16; ++x) {
        const auto offset = TextureByteOffset(texture_layout, x, y);
        if (offset % 4 || offset / 4 >= seen.size() || seen[offset / 4]) { bijective = false; } else { seen[offset / 4] = true; }
    } }
    check("thin1d_microtile_mapping_is_bounded_bijective", bijective && std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
    check("thin1d_known_morton_and_tile_row_vectors", TextureByteOffset(texture_layout, 1, 0) == 4 && TextureByteOffset(texture_layout, 0, 1) == 8 &&
        TextureByteOffset(texture_layout, 4, 0) == 64 && TextureByteOffset(texture_layout, 0, 4) == 128 && TextureByteOffset(texture_layout, 8, 0) == 256 && TextureByteOffset(texture_layout, 0, 8) == 512);
    std::vector<std::uint8_t> texture_backing(1024);
    for (unsigned tile = 0; tile < 4; ++tile) { for (unsigned pixel = 0; pixel < 64; ++pixel) {
        const auto x = (tile % 2) * 8 + (pixel & 1) + ((pixel >> 1) & 2) + ((pixel >> 2) & 4);
        const auto y = (tile / 2) * 8 + ((pixel >> 1) & 1) + ((pixel >> 2) & 2) + ((pixel >> 3) & 4);
        texture_backing[(tile * 64 + pixel) * 4] = static_cast<std::uint8_t>(y * 16 + x);
    } }
    const auto linear_texture = ReadTexturePixels(texture_layout, texture_backing); bool linear_exact = linear_texture.size() == 16 * 9 * 4;
    for (unsigned index = 0; index < 16 * 9; ++index) { linear_exact &= linear_texture[index * 4] == index; }
    check("texture_detile_crops_padded_rows_and_preserves_pixels", linear_exact);
    auto linear_words = texture_words; linear_words[3] = 0x90000000u | (8u << 20) | 0xfacu;
    const auto linear_layout = DescribeTexture(linear_words);
    check("linear_texture_uses_descriptor_pitch_without_tile_padding", linear_layout.bytes == 16 * 9 * 4 && TextureByteOffset(linear_layout, 3, 2) == 140);
    auto plane_words = linear_words; plane_words[1] = 1u << 20; plane_words[2] = 2u | (1u << 14); plane_words[4] = 4u << 13;
    const auto luma = DescribeTexture(plane_words);
    const std::array<std::uint8_t, 10> luma_bytes{10,20,30,99,99,40,50,60,99,99};
    const auto luma_rgba = ReadTexturePixels(luma, luma_bytes);
    check("linear_r8_expands_only_red_with_zero_gb_one_alpha_and_pitch", luma.channels == 1 && luma.bytes == 10 && TextureByteOffset(luma, 2, 1) == 7 &&
        luma_rgba == std::vector<std::uint8_t>{10,0,0,255,20,0,0,255,30,0,0,255,40,0,0,255,50,0,0,255,60,0,0,255});
    plane_words[1] = 3u << 20;
    const auto chroma = DescribeTexture(plane_words);
    const std::array<std::uint8_t, 20> chroma_bytes{10,110,20,120,30,130,99,99,99,99,40,140,50,150,60,160,99,99,99,99};
    check("linear_rg8_keeps_uv_order_and_missing_channel_defaults", chroma.channels == 2 && chroma.bytes == 20 && TextureByteOffset(chroma, 2, 1) == 14 &&
        ReadTexturePixels(chroma, chroma_bytes) == std::vector<std::uint8_t>{10,110,0,255,20,120,0,255,30,130,0,255,40,140,0,255,50,150,0,255,60,160,0,255});
    plane_words[1] = (1u << 20) | (9u << 26);
    check("r8_srgb_metadata_is_not_reinterpreted_as_four_source_channels", DescribeTexture(plane_words).srgb && DescribeTexture(plane_words).bytes == 10);
    plane_words[3] = texture_words[3];
    check("thin_microtile_requires_aligned_pitch_for_all_channel_widths", Rejects([&] { DescribeTexture(plane_words); }) &&
        Rejects([&] { auto words = plane_words; words[1] = 3u << 20; DescribeTexture(words); }));
    auto thin_words = texture_words; thin_words[1] = 1u << 20;
    const auto thin_r8 = DescribeTexture(thin_words);
    check("thin_r8_known_morton_addresses_and_byte_extent", thin_r8.channels == 1 && thin_r8.bytes == 256 &&
        TextureByteOffset(thin_r8, 1, 0) == 1 && TextureByteOffset(thin_r8, 0, 1) == 2 &&
        TextureByteOffset(thin_r8, 4, 0) == 16 && TextureByteOffset(thin_r8, 0, 4) == 32 &&
        TextureByteOffset(thin_r8, 7, 7) == 63 && TextureByteOffset(thin_r8, 8, 0) == 64 && TextureByteOffset(thin_r8, 0, 8) == 128);
    // Build tiled input from inverse Morton bits, independently of the address
    // function under test. Four tiles cover row and tile boundaries/padding.
    std::array<std::uint8_t, 256> thin_input{};
    std::array<std::uint8_t, 512> thin_dual{};
    for (unsigned tile = 0; tile < 4; ++tile) { for (unsigned pixel = 0; pixel < 64; ++pixel) {
        const auto x = (tile % 2) * 8 + (pixel & 1) + ((pixel >> 1) & 2) + ((pixel >> 2) & 4);
        const auto y = (tile / 2) * 8 + ((pixel >> 1) & 1) + ((pixel >> 2) & 2) + ((pixel >> 3) & 4);
        const auto value = static_cast<std::uint8_t>(y * 16 + x);
        thin_input[tile * 64 + pixel] = value;
        thin_dual[(tile * 64 + pixel) * 2] = value; thin_dual[(tile * 64 + pixel) * 2 + 1] = 255 - value;
    } }
    const auto thin_pixels = ReadTexturePixels(thin_r8, thin_input);
    bool thin_exact = thin_pixels.size() == 16 * 9 * 4;
    for (unsigned pixel = 0; pixel < 16 * 9; ++pixel) {
        thin_exact &= thin_pixels[pixel * 4] == pixel && thin_pixels[pixel * 4 + 1] == 0 && thin_pixels[pixel * 4 + 2] == 0 && thin_pixels[pixel * 4 + 3] == 255;
    }
    check("thin_r8_inverse_morton_input_expands_and_crops_exact_pixels", thin_exact);
    thin_words[1] = 3u << 20; const auto thin_rg8 = DescribeTexture(thin_words);
    const auto thin_dual_pixels = ReadTexturePixels(thin_rg8, thin_dual); bool dual_exact = thin_dual_pixels.size() == thin_pixels.size();
    for (unsigned pixel = 0; pixel < 16 * 9; ++pixel) {
        dual_exact &= thin_dual_pixels[pixel * 4] == pixel && thin_dual_pixels[pixel * 4 + 1] == 255 - pixel && thin_dual_pixels[pixel * 4 + 2] == 0 && thin_dual_pixels[pixel * 4 + 3] == 255;
    }
    check("thin_rg8_preserves_both_bytes_and_crosses_tile_rows", dual_exact && thin_rg8.bytes == 512 && TextureByteOffset(thin_rg8, 8, 8) == 384);
    thin_words[1] = 1u << 20; thin_words[2] = 31u | (31u << 14); thin_words[3] = 0x90000000u | (13u << 20) | 0x849u; thin_words[4] = 31u << 13;
    const auto alpha_atlas = DescribeTexture(thin_words);
    check("thin_r8_alpha_descriptor_keeps_one_one_one_red_swizzle", alpha_atlas.bytes == 1024 && alpha_atlas.components == std::array<unsigned, 4>{1,1,1,4});
    plane_words[1] = 10u << 20; plane_words[3] = linear_words[3];
    const auto rgba_rows = DescribeTexture(plane_words);
    std::vector<std::uint8_t> rgba_backing(40, 99); std::copy_n(luma_rgba.begin(), 12, rgba_backing.begin()); std::copy_n(luma_rgba.begin() + 12, 12, rgba_backing.begin() + 20);
    check("linear_rgba_row_copy_preserves_padding_and_exact_pixels", ReadTexturePixels(rgba_rows, rgba_backing) == luma_rgba);
    auto bad_texture = texture_words; bad_texture[3] |= 1u << 16;
    const bool mip_rejected = Rejects([&] { DescribeTexture(bad_texture); }); bad_texture = texture_words; bad_texture[1] = 35u << 20;
    check("unsupported_texture_mips_formats_and_extents_are_not_guessed", mip_rejected && Rejects([&] { DescribeTexture(bad_texture); }) && Rejects([&] { TextureByteOffset(texture_layout, 16, 0); }));
    check("texture_snapshot_size_must_match_exactly", Rejects([&] { ReadTexturePixels(texture_layout, {}); }) &&
        Rejects([&] { auto bad = linear_layout; bad.pitch = 1; ReadTexturePixels(bad, std::vector<std::uint8_t>(bad.bytes)); }));
    std::array<std::uint32_t, 4> sampler_words{146, 0xfff000, 0x09503f00, 0}; const auto sampler_state = DescribeSampler(sampler_words);
    check("sampler_clamp_filter_lod_and_signed_bias_decode", sampler_state.clamp == std::array<std::uint32_t, 3>{2, 2, 2} && sampler_state.linear_min && sampler_state.linear_mag &&
        sampler_state.linear_mip && sampler_state.bias == -1 && sampler_state.maximum_lod == 4095.0f / 256);
    sampler_words[0] |= 1u << 15;
    check("unnormalized_sampler_is_an_explicit_frontier", Rejects([&] { DescribeSampler(sampler_words); }));
    const auto stencil = DecodeGcnStencil(0x880, 0x01ffff00, 2);
    check("gcn_stencil_keeps_test_reference_and_wrap_operations", stencil.reference == 0 && stencil.compare == 2 && stencil.compare_mask == 255 && stencil.write_mask == 255 &&
        stencil.fail == 0 && stencil.pass == 6 && stencil.depth_fail == 6);
    check("independent_stencil_replace_reference_is_not_silently_merged", Rejects([] { DecodeGcnStencil(0x40, 0x01ffff00, 2); }) && DecodeGcnStencil(0x30, 0x01ffff00, 2).pass == 2);
    const std::array<std::uint32_t, 8> discard_pixel{0xbeeb03ffu, 7u, 0x7e0002f2u, 0x7e020280u, 0xbefe0480u, 0xf800180fu, 0x00010100u, 0xbf810000u};
    check("gcn_discard_declares_vulkan_1_2_demote_extension", ValidateSpirv(CompileShader(ShaderStage::Pixel, discard_pixel, shader, context, nullptr, nullptr).spirv));
    pm4::RegisterBank cache_shader, cache_context; cache_shader.known.set(); cache_context.known.set();
    ContextWord(cache_context, offsetof(AmdGpu::Regs, color_export_format), 9);
    ContextWord(cache_context, offsetof(AmdGpu::Regs, color_buffers) + offsetof(AmdGpu::ColorBuffer, info), 10u << 2);
    ShaderCache cache;
    const auto cached = cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr);
    check("shader_cache_reuses_identical_compilation", cached == cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr) && cache.Stats().hits == 1);
    Set(cache_context, 0x1e0, 0x65010504);
    check("shader_cache_ignores_registers_not_consumed_by_compiler", cached == cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr) && cache.Stats().hits == 2);
    cache_shader.known.reset(0x4a);
    check("shader_cache_does_not_hide_missing_consumed_register", Rejects([&] { cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr); }));
    Set(cache_shader, 0x4a, 1);
    const auto changed_register = cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr);
    check("shader_cache_invalidates_consumed_register_changes", changed_register != cached && ValidateSpirv(changed_register->spirv));
    const auto moved_origin = cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr, 0, 256);
    const auto moved_binding = cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr, 4, 256);
    check("shader_cache_key_includes_origin_and_first_binding", changed_register != moved_origin && moved_origin != moved_binding);
    Memory cache_memory; cache_memory.Word(256, 0x3f000000);
    Set(cache_shader, 11, 4); Set(cache_shader, 12, Memory::base + 256); Set(cache_shader, 13, 0);
    const auto scalar_first = cache.Get(ShaderStage::Pixel, scalar, cache_shader, cache_context, Memory::Read, &cache_memory);
    const auto scalar_same = cache.Get(ShaderStage::Pixel, scalar, cache_shader, cache_context, Memory::Read, &cache_memory);
    cache_memory.Word(256, 0x3e800000);
    const auto scalar_changed = cache.Get(ShaderStage::Pixel, scalar, cache_shader, cache_context, Memory::Read, &cache_memory);
    check("shader_cache_replays_srt_memory_and_invalidates_same_address_edits", scalar_same == scalar_first && scalar_changed != scalar_first &&
        scalar_changed->flat_constants[16] == 0x3e800000);
    cache_memory.fail = true;
    check("shader_cache_rejects_unreadable_previously_valid_memory", Rejects([&] { cache.Get(ShaderStage::Pixel, scalar, cache_shader, cache_context, Memory::Read, &cache_memory); }));
    ShaderCache tiny_cache{1, 1024 * 1024};
    const auto leased = tiny_cache.Get(ShaderStage::Vertex, vertex, cache_shader, cache_context, nullptr, nullptr);
    tiny_cache.Get(ShaderStage::Pixel, pixel, cache_shader, cache_context, nullptr, nullptr);
    check("shader_cache_eviction_does_not_invalidate_active_result_lease", tiny_cache.Stats().entries == 1 && tiny_cache.Stats().evictions == 1 && ValidateSpirv(leased->spirv));
    TextureDecodeCache decode_cache{4096, 1};
    const auto decoded = decode_cache.Decode(texture_layout, texture_backing);
    const bool decode_hit = decoded == decode_cache.Decode(texture_layout, texture_backing);
    texture_backing[0] = 73; const auto decoded_changed = decode_cache.Decode(texture_layout, texture_backing);
    check("texture_decode_cache_compares_full_snapshot_and_preserves_old_lease", decode_hit && decoded_changed != decoded && (*decoded_changed)[0] == 73 && (*decoded)[0] == 0 && decode_cache.evictions == 1);
    check("texture_decode_cache_bound_and_extent_are_enforced", decode_cache.Bytes() <= 4096 && Rejects([&] { decode_cache.Decode(texture_layout, {}); }));
    auto macro_words = texture_words;
    macro_words[0] = 0x10000; macro_words[1] = 10u << 20;
    macro_words[2] = 249u | (99u << 14); macro_words[4] = 255u << 13;
    macro_words[3] = 0x90000000u | (10u << 20) | 0xfacu;
    const auto macro_display = DescribeTexture(macro_words);
    check("display_macro_texture_decodes_independent_extent_and_padding", macro_display.width == 250 && macro_display.height == 100 &&
        macro_display.pitch == 256 && macro_display.padded_height == 128 && macro_display.bytes == 131072 && macro_display.tiling == 10);
    macro_words[3] = 0x90000000u | (14u << 20) | 0xfacu;
    const auto macro_thin = DescribeTexture(macro_words);
    check("thin_macro_has_morton_order_not_display_order", TextureByteOffset(macro_thin, 1, 0) == 4 &&
        TextureByteOffset(macro_thin, 0, 1) == 8 && TextureByteOffset(macro_thin, 4, 0) == 64 &&
        TextureByteOffset(macro_thin, 0, 4) == 128 && TextureByteOffset(macro_thin, 8, 0) == 256 &&
        TextureByteOffset(macro_thin, 0, 8) == 16640 && TextureByteOffset(macro_display, 0, 1) == 16);
    bool macro_bijective = true;
    for (const auto& layout : {macro_display, macro_thin}) {
        std::vector<bool> seen(static_cast<std::size_t>(layout.bytes / 4));
        for (unsigned y = 0; y < layout.padded_height; ++y) { for (unsigned x = 0; x < layout.pitch; ++x) {
            const auto offset = TextureByteOffset(layout, x, y);
            if (offset % 4 || offset / 4 >= seen.size() || seen[offset / 4]) { macro_bijective = false; }
            else { seen[offset / 4] = true; }
        } }
        macro_bijective &= std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
    }
    check("both_macro_layouts_are_bijective_across_macro_rows_and_columns", macro_bijective);
    std::vector<std::uint8_t> macro_backing(static_cast<std::size_t>(macro_thin.bytes), 73), macro_pixels(250 * 100 * 4);
    for (std::size_t i = 0; i < macro_pixels.size(); ++i) { macro_pixels[i] = static_cast<std::uint8_t>(i * 37 + i / 1000); }
    WriteTexturePixels(macro_thin, macro_pixels, macro_backing);
    check("offscreen_writeback_roundtrip_keeps_row_and_height_padding", ReadTexturePixels(macro_thin, macro_backing) == macro_pixels &&
        macro_backing[TextureByteOffset(macro_thin, 250, 1)] == 73 && macro_backing[TextureByteOffset(macro_thin, 0, 100)] == 73 &&
        macro_backing[8] == macro_pixels[250 * 4]);
    TextureDecodeCache macro_cache; const auto before_write = macro_cache.Decode(macro_thin, macro_backing);
    macro_pixels[0] ^= 0xff; WriteTexturePixels(macro_thin, macro_pixels, macro_backing);
    const auto after_write = macro_cache.Decode(macro_thin, macro_backing);
    check("sampling_revalidates_offscreen_writeback_at_same_address", before_write != after_write &&
        *after_write == macro_pixels && (*before_write)[0] != (*after_write)[0]);
    macro_backing.back() ^= 0xff;
    const auto padding_changed = macro_cache.Decode(macro_thin, macro_backing);
    check("borrowed_texture_view_checks_nonvisible_tail_padding_not_only_visible_pixels", padding_changed != after_write &&
        *padding_changed == *after_write && macro_cache.misses == 3);
    std::shared_ptr<const std::vector<std::uint8_t>> detached_pixels;
    {
        TextureDecodeCache temporary_cache;
        auto borrowed = macro_backing;
        detached_pixels = temporary_cache.Decode(macro_thin, borrowed);
        std::fill(borrowed.begin(), borrowed.end(), 0);
    }
    check("borrowed_texture_view_and_cache_can_expire_without_invalidating_pixels", *detached_pixels == macro_pixels);
    {
        auto large_words = texture_words;
        large_words[0] = 0x20000; large_words[1] = 10u << 20;
        large_words[2] = 4095u | (2047u << 14); large_words[4] = 4095u << 13;
        large_words[3] = 0x90000000u | (8u << 20) | 0xfacu;
        const auto large_layout = DescribeTexture(large_words);
        std::vector<std::uint8_t> borrowed(static_cast<std::size_t>(large_layout.bytes), 73);
        TextureDecodeCache large_cache;
        const auto original = large_cache.Decode(large_layout, borrowed);
        const auto hit = large_cache.Decode(large_layout, borrowed);
        check("borrowed_32mib_atlas_hit_preserves_immutable_pixel_identity", large_layout.bytes == 32 * 1024 * 1024 &&
            hit == original && large_cache.hits == 1 && large_cache.misses == 1 && (*original).back() == 73);
        borrowed.back() = 19;
        const auto changed = large_cache.Decode(large_layout, borrowed);
        check("borrowed_32mib_atlas_detects_last_byte_update_at_unchanged_address", changed != original &&
            changed->back() == 19 && original->back() == 73 && large_cache.misses == 2);
    }
    {
        auto atlas_words = texture_words;
        atlas_words[1] = 10u << 20; atlas_words[2] = 4095u | (2047u << 14);
        atlas_words[3] = 0x90000000u | (8u << 20) | 0xfacu; atlas_words[4] = 4095u << 13;
        std::array<TextureLayout, 4> layouts;
        std::array<std::vector<std::uint8_t>, 4> backing;
        for (unsigned i = 0; i < layouts.size(); ++i) {
            atlas_words[0] = 0x20000u + i * 0x20000u;
            layouts[i] = DescribeTexture(atlas_words);
            backing[i].resize(static_cast<std::size_t>(layouts[i].bytes), static_cast<std::uint8_t>(31 + i));
        }
        TextureDecodeCache working_set;
        bool exact = true;
        for (unsigned round = 0; round < 2; ++round) {
            for (unsigned i = 0; i < layouts.size(); ++i) {
                const auto image = working_set.Decode(layouts[i], backing[i]);
                exact &= image->front() == 31 + i && image->back() == 31 + i;
            }
        }
        check("default_texture_cache_retains_four_32mib_atlases_without_redecode_cycle", exact &&
            working_set.hits == 4 && working_set.misses == 4 && working_set.evictions == 0 && working_set.Bytes() == 256 * 1024 * 1024);
    }
    auto bad_macro = macro_words; bad_macro[1] = 1u << 20;
    const bool macro_r8_rejected = Rejects([&] { DescribeTexture(bad_macro); }); bad_macro = macro_words; bad_macro[4] = 249u << 13;
    check("macro_texture_rejects_unimplemented_bpp_pitch_and_mip", macro_r8_rejected && Rejects([&] { DescribeTexture(bad_macro); }) &&
        Rejects([&] { auto words = macro_words; words[3] |= 1u << 16; DescribeTexture(words); }));
    check("offscreen_writeback_rejects_bad_extents_before_mutating", Rejects([&] { WriteTexturePixels(macro_thin, {}, macro_backing); }) &&
        Rejects([&] { WriteTexturePixels(macro_thin, macro_pixels, {}); }) && ReadTexturePixels(macro_thin, macro_backing) == macro_pixels);
    Memory fetch_cached_memory; fetch_cached_memory.Word(0, 0x7e0002f0); fetch_cached_memory.Word(4, 0xbe802000);
    Set(cache_shader, 0x4b, 6u << 1); Set(cache_shader, 0x50, Memory::base); Set(cache_shader, 0x51, 0);
    const auto fetch_before = cache.Get(ShaderStage::Vertex, fetched_vertex, cache_shader, cache_context, Memory::Read, &fetch_cached_memory);
    fetch_cached_memory.Word(0, 0x7e0002f1);
    const auto fetch_after = cache.Get(ShaderStage::Vertex, fetched_vertex, cache_shader, cache_context, Memory::Read, &fetch_cached_memory);
    check("shader_cache_revalidates_inline_fetch_code_at_unchanged_address", fetch_before != fetch_after && fetch_before->spirv != fetch_after->spirv);
    std::printf("{\"event\":\"shader_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}

std::vector<std::uint32_t> AuthoredVertexProgram(bool quad) {
    // Authored procedural three-corner VS, independent of game executables.
    Sirit::Module module{0x00010500};
    module.AddCapability(spv::Capability::Shader);
    module.SetMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
    const auto f32 = module.TypeFloat(32), u32 = module.TypeUInt(32), boolean = module.TypeBool();
    const auto vec4 = module.TypeVector(f32, 4);
    // Match the production compiler's standalone built-in output, rather
    // than declaring an incompatible one-member gl_PerVertex interface block.
    const auto position = module.AddGlobalVariable(module.TypePointer(spv::StorageClass::Output, vec4), spv::StorageClass::Output);
    module.Decorate(position, spv::Decoration::BuiltIn, static_cast<unsigned>(spv::BuiltIn::Position));
    const auto index = module.AddGlobalVariable(module.TypePointer(spv::StorageClass::Input, u32), spv::StorageClass::Input);
    module.Decorate(index, spv::Decoration::BuiltIn, spv::BuiltIn::VertexIndex);
    const auto entry = module.OpFunction(module.TypeVoid(), spv::FunctionControlMask::MaskNone, module.TypeFunction(module.TypeVoid()));
    module.AddEntryPoint(spv::ExecutionModel::Vertex, entry, "main", position, index);
    module.AddLabel(module.OpLabel());
    const auto id = module.OpLoad(u32, index), minus = module.Constant(f32, -1.0f), plus = module.Constant(f32, 1.0f);
    auto right = module.OpIEqual(boolean, id, module.Constant(u32, 1u));
    if (quad) { right = module.OpLogicalOr(boolean, right, module.OpIEqual(boolean, id, module.Constant(u32, 2u))); }
    const auto x = module.OpSelect(f32, right, plus, minus);
    const auto y = module.OpSelect(f32, quad ? module.OpUGreaterThanEqual(boolean, id, module.Constant(u32, 2u)) : module.OpIEqual(boolean, id, module.Constant(u32, 2u)), plus, minus);
    module.OpStore(position, module.OpCompositeConstruct(vec4, x, y, module.Constant(f32, 0.5f), plus));
    module.OpReturn(); module.OpFunctionEnd();
    return module.Assemble();
}
std::vector<std::uint32_t> AuthoredSamplePixelProgram(float u, float v) { return SamplePixel(u, v); }
CompiledShader AuthoredSolidPixelProgram() {
    pm4::RegisterBank shader, context; shader.known.set(); context.known.set();
    ContextWord(context, offsetof(AmdGpu::Regs, color_export_format), 9);
    ContextWord(context, offsetof(AmdGpu::Regs, color_buffers) + offsetof(AmdGpu::ColorBuffer, info), static_cast<unsigned>(AmdGpu::DataFormat::Format8_8_8_8) << 2);
    return CompileShader(ShaderStage::Pixel, pixel, shader, context, nullptr, nullptr);
}
bool RasterSelfTest(VulkanDevice& device) {
    const auto vs = AuthoredVertexProgram(false), quad_vs = AuthoredVertexProgram(true);
    const auto ps = AuthoredSolidPixelProgram();
    pm4::RegisterBank shader, context; shader.known.set(); context.known.set();
    ContextWord(context, offsetof(AmdGpu::Regs, color_export_format), 9);
    ContextWord(context, offsetof(AmdGpu::Regs, color_buffers) + offsetof(AmdGpu::ColorBuffer, info), static_cast<unsigned>(AmdGpu::DataFormat::Format8_8_8_8) << 2);
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value; std::printf("{\"event\":\"raster_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
    };
    // Vulkan property bits: DEVICE_LOCAL=1, HOST_VISIBLE=2, COHERENT=4,
    // CACHED=8. A preferred cached type must still satisfy compatibility.
    constexpr std::array<std::uint32_t, 4> memory_flags{1, 6, 2, 14};
    check("readback_memory_prefers_host_cached_after_required_flags", SelectMemoryType(memory_flags, 15, 6, 8) == 3);
    check("readback_memory_fallback_preserves_resource_mask_and_coherency", SelectMemoryType(memory_flags, 7, 6, 8) == 1 &&
        SelectMemoryType(memory_flags, 4, 6, 8) == UINT32_MAX && SelectMemoryType(memory_flags, 15, 6) == 1);
    constexpr std::array<std::uint32_t, 33> oversized_memory{};
    check("memory_type_selection_rejects_empty_and_out_of_range_lists", SelectMemoryType({}, ~0u, 0, 0) == UINT32_MAX &&
        SelectMemoryType(oversized_memory, ~0u, 0, 0) == UINT32_MAX && SelectMemoryType(memory_flags, 0, 6, 8) == UINT32_MAX);
    check("authored_rectangle_and_quad_programs_validate", ValidateSpirv(vs) && ValidateSpirv(quad_vs) && ValidateSpirv(ps.rectangle_control) && ValidateSpirv(ps.quad_control) && ValidateSpirv(ps.rectangle_evaluation));
    std::vector<std::uint8_t> initial(32 * 16 * 4);
    for (std::size_t i = 0; i < initial.size(); i += 4) { initial[i + 2] = initial[i + 3] = 255; }
    RasterDraw draw{}; draw.width = 32; draw.height = 16; draw.vertices = 3; draw.rectangle = true;
    draw.vertex = vs; draw.pixel = ps.spirv; draw.rectangle_control = ps.rectangle_control; draw.rectangle_evaluation = ps.rectangle_evaluation;
    draw.color = initial; draw.viewport = {0, 0, 32, 16, 0, 1}; draw.scissor = {4, 3, 24, 10};
    auto result = device.Draw(draw);
    bool exact = result.color.size() == initial.size();
    for (unsigned y0 = 0; y0 < 16; ++y0) {
        for (unsigned x0 = 0; x0 < 32; ++x0) {
            const auto offset = (y0 * 32 + x0) * 4;
            const bool inside = x0 >= 4 && x0 < 28 && y0 >= 3 && y0 < 13;
            exact &= result.color[offset] == (inside ? 255 : 0) && result.color[offset + 1] == 0 &&
                result.color[offset + 2] == (inside ? 0 : 255) && result.color[offset + 3] == 255;
        }
    }
    check("gpu_rectangle_fills_fourth_corner_and_preserves_pixels_outside_scissor", exact);
    draw.rectangle = false; draw.quad = true; draw.vertices = 4; draw.vertex = quad_vs; draw.rectangle_control = ps.quad_control;
    const auto quad_result = device.Draw(draw);
    check("gpu_quad_list_renders_all_four_vertices_and_preserves_scissor", quad_result.color == result.color);
    auto incomplete_quad = draw; incomplete_quad.vertices = 3;
    check("incomplete_quad_is_rejected_before_gpu_submission", Rejects([&] { device.Draw(incomplete_quad); }));
    draw.rectangle = true; draw.quad = false; draw.vertices = 3; draw.vertex = vs; draw.rectangle_control = ps.rectangle_control;
    const std::array<std::uint32_t, 8> translucent_pixel{0xbeeb03ffu, 7u, 0x7e0002f2u, 0x7e020280u, 0x7e0402f0u, 0xf800180fu, 0x02010100u, 0xbf810000u};
    const auto translucent = CompileShader(ShaderStage::Pixel, translucent_pixel, shader, context, nullptr, nullptr);
    draw.pixel = translucent.spirv; draw.blend = DecodeGcnBlend(0x65010504);
    result = device.Draw(draw);
    const auto center = (8 * 32 + 16) * 4;
    check("gpu_alpha_blending_preserves_destination_and_separate_alpha", result.color[center] >= 127 && result.color[center] <= 128 &&
        result.color[center + 1] == 0 && result.color[center + 2] >= 127 && result.color[center + 2] <= 128 && result.color[center + 3] == 255 && result.color[2] == 255);
    draw.color_mask = 1;
    result = device.Draw(draw);
    check("gpu_blend_honors_component_write_mask", result.color[center] >= 127 && result.color[center] <= 128 && result.color[center + 2] == 255 && result.color[center + 3] == 255);
    draw.pixel = ps.spirv; draw.blend = {}; draw.color_mask = 15;
    std::array<std::uint8_t, 16> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    RasterTexture texture; texture.width = texture.height = 2; texture.pixels = texels;
    RasterSampler sampler; sampler.binding = 1; sampler.clamp = {2, 2, 2};
    const auto sample_pixel = SamplePixel(0.25f, 0.75f), center_sample = SamplePixel(0.5f, 0.5f);
    check("authored_sampled_image_programs_validate", ValidateSpirv(sample_pixel) && ValidateSpirv(center_sample));
    draw.textures = {&texture, 1}; draw.samplers = {&sampler, 1}; draw.pixel = sample_pixel;
    result = device.Draw(draw);
    check("gpu_nearest_texture_sampling_reads_uploaded_texel", result.color[center] == 0 && result.color[center + 1] == 0 && result.color[center + 2] == 255 && result.color[center + 3] == 255);
    const auto repeated_texture = device.Draw(draw);
    check("gpu_transient_buffer_pool_reuses_only_retired_storage", repeated_texture.buffer_cache_hits >= 2 &&
        repeated_texture.buffer_creates == 0 && repeated_texture.color == result.color);
    check("gpu_attachment_pool_reuses_retired_images_without_reusing_contents", repeated_texture.attachment_cache_hits == 1 &&
        repeated_texture.attachment_creates == 0 && repeated_texture.color == result.color);
    auto changed_initial = initial; changed_initial[0] = 137;
    draw.color = changed_initial; const auto fresh_upload = device.Draw(draw); draw.color = initial;
    check("gpu_reused_upload_buffer_receives_fresh_input_bytes", fresh_upload.buffer_cache_hits >= 2 &&
        fresh_upload.color[0] == 137 && fresh_upload.color[center] == result.color[center]);
    check("gpu_texture_cache_skips_unchanged_upload_and_preserves_pixels", repeated_texture.texture_cache_hits == 1 && repeated_texture.texture_uploads == 0 && repeated_texture.color == result.color);
    check("gpu_pipeline_cache_reuses_identical_program_and_state", repeated_texture.pipeline_cache_hit && repeated_texture.color == result.color);
    const auto saved_texels = texels; texels[8] = 255; texels[10] = 0;
    const auto changed_texture = device.Draw(draw);
    check("gpu_texture_cache_does_not_reuse_changed_pixels_at_same_pointer", changed_texture.texture_uploads == 1 && changed_texture.color[center] == 255 && changed_texture.color[center + 2] == 0);
    texels = saved_texels;
    device.TrimRasterCaches(); const auto trimmed_stats = device.CacheStats();
    const auto after_trim = device.Draw(draw);
    check("gpu_texture_cache_trim_retires_images_and_next_draw_reuploads", trimmed_stats.entries == 0 && trimmed_stats.bytes == 0 &&
        trimmed_stats.pipeline_entries == 0 && trimmed_stats.pipeline_key_bytes == 0 &&
        trimmed_stats.buffer_entries == 0 && trimmed_stats.buffer_bytes == 0 && after_trim.buffer_creates >= 2 && !after_trim.pipeline_cache_hit &&
        trimmed_stats.attachment_entries == 0 && trimmed_stats.attachment_bytes == 0 && after_trim.attachment_creates == 1 &&
        after_trim.texture_uploads == 1 && after_trim.color == result.color);
    device.TrimRasterCaches();
    texture.snapshot = std::make_shared<const std::vector<std::uint8_t>>(texels.begin(), texels.end());
    texture.pixels = *texture.snapshot; const auto immutable_first = device.Draw(draw), immutable_again = device.Draw(draw);
    check("gpu_immutable_texture_snapshot_reuses_owned_identity", immutable_first.texture_uploads == 1 &&
        immutable_again.texture_cache_hits == 1 && immutable_again.texture_uploads == 0 && immutable_again.color == result.color);
    auto next_pixels = texels; next_pixels[8] = 255; next_pixels[10] = 0;
    texture.snapshot = std::make_shared<const std::vector<std::uint8_t>>(next_pixels.begin(), next_pixels.end());
    texture.pixels = *texture.snapshot; const auto new_snapshot = device.Draw(draw);
    check("gpu_replacement_immutable_snapshot_uploads_changed_content", new_snapshot.texture_uploads == 1 &&
        new_snapshot.color[center] == 255 && new_snapshot.color[center + 2] == 0);
    texture.pixels = texels;
    check("gpu_snapshot_identity_requires_exact_owned_span", Rejects([&] { device.Draw(draw); }));
    texture.snapshot.reset();
    auto mutable_program = sample_pixel;
    draw.pixel = mutable_program; const auto unchanged_program = device.Draw(draw);
    bool mutated{};
    for (std::size_t offset = 5; offset < mutable_program.size();) {
        const auto count = mutable_program[offset] >> 16;
        if (!count || count > mutable_program.size() - offset) { throw std::runtime_error("Authored shader mutation test instruction shape changed"); }
        // OpConstant float32: change the authored X coordinate .25 to .75,
        // without resizing or replacing the original SPIR-V backing storage.
        if ((mutable_program[offset] & 0xffff) == 43 && count == 4 && mutable_program[offset + 3] == 0x3e800000) {
            mutable_program[offset + 3] = 0x3f400000; mutated = true;
        }
        offset += count;
    }
    if (!mutated || !ValidateSpirv(mutable_program)) { throw std::runtime_error("Authored shader mutation test constants changed"); }
    const auto changed_program = device.Draw(draw);
    draw.pixel = sample_pixel; const auto restored_program = device.Draw(draw);
    check("gpu_pipeline_cache_compares_program_words_not_source_pointer", unchanged_program.pipeline_cache_hit &&
        !changed_program.pipeline_cache_hit && changed_program.color != unchanged_program.color &&
        restored_program.pipeline_cache_hit && restored_program.color == unchanged_program.color);
    const auto saved_constants = draw.blend.constants;
    const auto evictions_before = device.CacheStats().pipeline_evictions;
    for (unsigned index = 1; index <= 65; ++index) {
        draw.blend.constants[0] = static_cast<float>(index) / 128;
        (void)device.Draw(draw);
    }
    const auto bounded_stats = device.CacheStats();
    draw.blend.constants = saved_constants;
    const auto recreated_pipeline = device.Draw(draw);
    check("gpu_pipeline_cache_is_bounded_and_evicted_state_recreates_identical_pixels", bounded_stats.pipeline_entries == 64 &&
        bounded_stats.pipeline_key_bytes <= 16ull * 1024 * 1024 && bounded_stats.pipeline_evictions > evictions_before &&
        !recreated_pipeline.pipeline_cache_hit && recreated_pipeline.color == restored_program.color);
    texture.components = {6, 5, 4, 7}; result = device.Draw(draw);
    check("gpu_sampled_image_view_applies_descriptor_swizzle", result.color[center] == 255 && result.color[center + 2] == 0 && result.texture_uploads == 1);
    texture.components = {4, 5, 6, 7}; sampler.linear_min = sampler.linear_mag = true; draw.pixel = center_sample;
    result = device.Draw(draw);
    check("gpu_bilinear_texture_sampling_interpolates_four_texels", result.color[center] >= 127 && result.color[center] <= 128 &&
        result.color[center + 1] >= 127 && result.color[center + 1] <= 128 && result.color[center + 2] >= 127 && result.color[center + 2] <= 128);
    const auto discard_sample = SamplePixel(0.25f, 0.75f, true); draw.pixel = discard_sample; result = device.Draw(draw);
    check("gpu_demoted_fragment_does_not_change_attachment", result.color == initial);
    // Real GPU render -> Thin2DThin guest storage -> sampled texture in a
    // second draw. The display-color input remains independent throughout.
    TextureLayout offscreen; offscreen.address = 0x100000; offscreen.width = 32; offscreen.height = 16;
    offscreen.pitch = 128; offscreen.padded_height = 64; offscreen.bytes = 32768; offscreen.tiling = 14;
    std::vector<std::uint8_t> offscreen_backing(32768, 73);
    auto offscreen_draw = draw; offscreen_draw.pixel = ps.spirv; offscreen_draw.textures = {}; offscreen_draw.samplers = {};
    const auto offscreen_result = device.Draw(offscreen_draw); WriteTexturePixels(offscreen, offscreen_result.color, offscreen_backing);
    const auto offscreen_pixels = ReadTexturePixels(offscreen, offscreen_backing);
    RasterTexture offscreen_texture; offscreen_texture.width = 32; offscreen_texture.height = 16; offscreen_texture.pixels = offscreen_pixels;
    auto sampled_draw = draw; sampled_draw.pixel = center_sample; sampled_draw.textures = {&offscreen_texture, 1};
    const auto sampled_result = device.Draw(sampled_draw);
    check("gpu_offscreen_render_tiled_writeback_and_second_pass_sampling", sampled_result.color[center] == 255 &&
        sampled_result.color[center + 1] == 0 && sampled_result.color[center + 2] == 0 && sampled_result.color[center + 3] == 255 &&
        initial[center] == 0 && initial[center + 2] == 255 && offscreen_backing[TextureByteOffset(offscreen, 32, 0)] == 73 &&
        offscreen_backing[TextureByteOffset(offscreen, 0, 16)] == 73);
    draw.pixel = ps.spirv; draw.textures = {}; draw.samplers = {};
    draw.stencil = true; std::vector<std::uint8_t> stencil_input(512); draw.stencil_data = stencil_input;
    draw.stencil_front = draw.stencil_back = DecodeGcnStencil(0x880, 0x01ffff00, 2);
    result = device.Draw(draw);
    check("gpu_stencil_compare_and_increment_wrap_are_fence_visible", result.stencil[8 * 32 + 16] == 1 && result.stencil[0] == 0 && result.color[center] == 255);
    check("stencil_only_draw_does_not_allocate_or_return_depth_readback", result.depth.empty() && result.stencil.size() == 512);
    stencil_input = result.stencil; draw.stencil_data = stencil_input;
    draw.stencil_front = draw.stencil_back = DecodeGcnStencil(0, 0x01ffff00, 2);
    result = device.Draw(draw);
    check("gpu_failed_stencil_test_preserves_color_and_stencil", result.color == initial && result.stencil == stencil_input);
    draw.stencil = false; draw.stencil_data = {};
    draw.depth = draw.depth_write = true; draw.depth_compare = 1;
    std::vector<float> depth_input(512, 0.25f); draw.depth_data = depth_input;
    const auto failed_depth = device.Draw(draw); std::fill(depth_input.begin(), depth_input.end(), 0.75f);
    result = device.Draw(draw);
    check("gpu_nonclear_depth_test_loads_compares_and_writes", failed_depth.color == initial && failed_depth.depth[8 * 32 + 16] == 0.25f &&
        result.color[center] == 255 && result.depth[8 * 32 + 16] == 0.5f && result.depth[0] == 0.75f);
    check("depth_only_draw_does_not_allocate_or_return_stencil_readback", result.stencil.empty() && result.depth.size() == 512 && failed_depth.stencil.empty());
    draw.depth_data = {};
    draw.depth = draw.stencil = draw.clear_depth = draw.clear_stencil = true; draw.depth_clear = 0.25f; draw.stencil_clear = 73;
    result = device.Draw(draw);
    check("gpu_depth_and_stencil_clear_are_fence_visible", result.depth.size() == 512 && result.stencil.size() == 512 &&
        std::all_of(result.depth.begin(), result.depth.end(), [](float value) { return value == 0.25f; }) &&
        std::all_of(result.stencil.begin(), result.stencil.end(), [](auto value) { return value == 73; }));
    auto invalid = draw; invalid.color = {};
    check("bad_input_rejected_without_gpu_submission", Rejects([&] { device.Draw(invalid); }));
    const auto buffer_evictions_before = device.CacheStats().buffer_evictions;
    std::vector<std::uint8_t> unused_storage;
    RasterBuffer unused_binding{29, true, {}};
    for (unsigned index = 1; index <= 65; ++index) {
        unused_storage.resize(index * 16); unused_binding.bytes = unused_storage; draw.buffers = {&unused_binding, 1};
        result = device.Draw(draw);
    }
    const auto pooled = device.CacheStats();
    check("gpu_transient_buffer_pool_is_bounded_and_eviction_preserves_pixels", pooled.buffer_entries <= 64 &&
        pooled.buffer_bytes <= 256ull * 1024 * 1024 && pooled.buffer_evictions > buffer_evictions_before &&
        result.color[center] == 255 && result.depth[8 * 32 + 16] == 0.25f && result.stencil[8 * 32 + 16] == 73);
    const auto attachment_evictions_before = device.CacheStats().attachment_evictions;
    auto resized = draw; std::vector<std::uint8_t> resized_color;
    for (unsigned index = 1; index <= 5; ++index) {
        resized.width = 32 + index * 8; resized_color.assign(resized.width * resized.height * 4, 0); resized.color = resized_color;
        resized.scissor = {0, 0, static_cast<int>(resized.width), static_cast<int>(resized.height)};
        resized.viewport = {0, 0, static_cast<float>(resized.width), static_cast<float>(resized.height), 0, 1};
        (void)device.Draw(resized);
    }
    const auto attachment_pool = device.CacheStats(); const auto recreated_attachment = device.Draw(draw);
    check("gpu_attachment_pool_matches_extents_and_recreates_evicted_images", attachment_pool.attachment_entries <= 8 &&
        attachment_pool.attachment_bytes <= 128ull * 1024 * 1024 && attachment_pool.attachment_evictions > attachment_evictions_before &&
        recreated_attachment.attachment_creates == 2 && recreated_attachment.color == result.color &&
        recreated_attachment.depth == result.depth && recreated_attachment.stencil == result.stencil);
    std::printf("{\"event\":\"raster_self_test\",\"passed\":%s}\n", passed ? "true" : "false"); return passed;
}
}
