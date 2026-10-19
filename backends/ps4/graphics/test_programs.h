#pragma once
#include "shader_compiler.h"

namespace kt::gfx {
// Authored test programs shared by graphics and native-memory integration
// tests. No commercial executable or shader data is included.
std::vector<std::uint32_t> AuthoredVertexProgram(bool quad = false);
std::vector<std::uint32_t> AuthoredSamplePixelProgram(float u, float v);
CompiledShader AuthoredSolidPixelProgram();
}
