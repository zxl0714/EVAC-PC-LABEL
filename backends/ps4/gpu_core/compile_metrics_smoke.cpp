#include "compile_metrics.h"
#include <stdexcept>
#include <thread>

int main() {
    using namespace Ketsui::GpuCore;
    { ScopedCompileMetrics scope{CompileKind::Shader, 0x1234}; }
    try {
        ScopedCompileMetrics scope{CompileKind::GraphicsPipeline, 0x2345};
        throw std::runtime_error("authored unwinding fixture");
    } catch (const std::runtime_error&) {}
    { ScopedCompileMetrics scope{CompileKind::ComputePipeline, 0x3456}; }
    std::thread worker([] { ScopedCompileMetrics scope{CompileKind::Shader, 0x4567}; });
    { ScopedCompileMetrics scope{CompileKind::Shader, 0x5678}; }
    worker.join();
    { ScopedCompileMetrics scope{CompileKind::TilingPipeline, 0x6789}; }
    return 0;
}
