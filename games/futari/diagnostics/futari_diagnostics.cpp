// Title-specific module hooks use the collection's shared crash reporter.
#include "futari_diagnostics.h"
#include "diagnostics/crash_reporter.h"
void FutariDiagnosticsModule(const char* path,const char* phase,uint64_t memory_base,uint32_t image_base,uint32_t image_size) {
    cave::xbox360::CrashModule(path,phase,memory_base,image_base,image_size);
}
