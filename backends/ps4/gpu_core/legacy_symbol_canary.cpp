#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<unsigned> calls{};
}
namespace Shader {
struct Profile;
struct RuntimeInfo;
namespace IR {
struct Program;
}
namespace Backend {
struct Bindings;
namespace SPIRV {
// Deliberately the compiler's exact C++ symbol, in the EXE, not its DLL.
__declspec(dllexport) std::vector<std::uint32_t> EmitSPIRV(const Profile &,
                                                           const RuntimeInfo &,
                                                           const IR::Program &,
                                                           Bindings &) {
  ++calls;
  throw std::runtime_error("The GPU DLL resolved the host compiler canary");
}
} // namespace SPIRV
} // namespace Backend
} // namespace Shader
unsigned LegacyCompilerCanaryCalls() { return calls.load(); }
