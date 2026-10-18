#pragma once
#include <cstdint>

void FutariInitDiagnostics();
void FutariDiagnosticsModule(const char* path, const char* phase,
                            uint64_t memory_base = 0, uint32_t image_base = 0,
                            uint32_t image_size = 0);
