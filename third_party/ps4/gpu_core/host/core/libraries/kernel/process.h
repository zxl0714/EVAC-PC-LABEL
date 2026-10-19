#pragma once
#include "host_services.h"

namespace Libraries::Kernel {
inline int sceKernelIsNeoMode() { return Ketsui::GpuCore::IsNeoMode() ? 1 : 0; }
} // namespace Libraries::Kernel
