#include "shader_recompiler/ir/program.h"
namespace Shader::IR {
// The library does not own a user directory. The embedding runtime owns any
// diagnostic export and may use the retained IR/bytecode directly.
void DumpProgram(const Program&, const Info&, const std::string&) {}
}
