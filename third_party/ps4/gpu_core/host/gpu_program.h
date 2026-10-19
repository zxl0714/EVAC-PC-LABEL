#pragma once
#include "shader_recompiler/params.h"
#include <map>
#include <vector>
namespace Ketsui::GpuCore {
struct ProgramStore { std::map<u64, std::vector<u32>> programs; };
class ProgramScope {
    ProgramStore* previous;
public:
    explicit ProgramScope(ProgramStore&);
    ~ProgramScope();
    ProgramScope(const ProgramScope&) = delete;
};
Shader::ShaderParams ReadProgram(VAddr address, std::span<const u32, 16> user_data);
}
