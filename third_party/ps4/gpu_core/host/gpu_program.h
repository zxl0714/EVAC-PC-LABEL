#pragma once
#include "shader_recompiler/params.h"
#include <map>
#include <vector>
namespace Ketsui::GpuCore {
struct ProgramStore {
    std::map<u64, std::vector<u32>> programs;
    // Only read extents are remembered. Code and metadata are reread on every
    // lookup, including reuse of the same address after guest writes/remapping.
    std::map<u64, u32> read_extents;
    std::vector<u32> read_scratch;
    bool scratch_leased{};
};
class ProgramScope {
    ProgramStore* previous;
public:
    explicit ProgramScope(ProgramStore&);
    ~ProgramScope();
    ProgramScope(const ProgramScope&) = delete;
};
Shader::ShaderParams ReadProgram(VAddr address, std::span<const u32, 16> user_data);
}
