#pragma once
#include "pm4.h"
#include <cstddef>
#include <array>
#include <memory>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kt::gfx {
enum class ShaderStage { Vertex, Pixel };
using ShaderMemoryReader = bool(*)(void*, std::uint64_t, void*, std::size_t);
struct ShaderBufferBinding {
    std::uint32_t binding{};
    std::uint64_t address{}, bytes{};
    bool flat_constants{}, writable{};
};
struct ShaderImageBinding {
    std::uint32_t binding{};
    std::array<std::uint32_t, 8> descriptor{};
    bool unsupported_access{};
};
struct ShaderSamplerBinding {
    std::uint32_t binding{};
    std::array<std::uint32_t, 4> descriptor{};
};
struct CompiledShader {
    std::vector<std::uint32_t> spirv, flat_constants;
    std::vector<std::uint32_t> rectangle_control, quad_control, rectangle_evaluation;
    std::vector<ShaderBufferBinding> buffers;
    std::vector<ShaderImageBinding> images;
    std::vector<ShaderSamplerBinding> samplers;
    std::uint32_t buffer_bindings{}, image_bindings{}, sampler_bindings{};
};
struct ShaderCacheStats { std::uint64_t hits{}, misses{}, evictions{}, bytes{}; std::size_t entries{}; };
// Cache validation replays every compiler memory read and checks precisely the
// registers the compilation consumed. Results never own guest pointers.
class ShaderCache {
public:
    explicit ShaderCache(std::size_t maximum_entries = 128, std::uint64_t maximum_bytes = 64 * 1024 * 1024);
    ~ShaderCache();
    std::shared_ptr<const CompiledShader> Get(ShaderStage stage, std::span<const std::uint32_t> code,
        const pm4::RegisterBank& shader, const pm4::RegisterBank& context, ShaderMemoryReader reader,
        void* reader_context, std::uint32_t first_binding = 0, std::uint64_t guest_program_address = 0);
    ShaderCacheStats Stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Registers must describe this draw, not merely the last state of a submit.
// The isolated compiler never dereferences a guest address without the reader.
CompiledShader CompileShader(ShaderStage stage, std::span<const std::uint32_t> code,
    const pm4::RegisterBank& shader, const pm4::RegisterBank& context,
    ShaderMemoryReader reader, void* reader_context, std::uint32_t first_binding = 0, std::uint64_t guest_program_address = 0);
bool ValidateSpirv(std::span<const std::uint32_t> words, std::string* diagnostic = nullptr);
bool ShaderSelfTest();
class VulkanDevice;
bool RasterSelfTest(VulkanDevice& device);
bool AttachmentSelfTest(VulkanDevice& device);
}
