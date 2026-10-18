#pragma once
#include <cstdint>
#include <filesystem>
namespace espgaluda2 {
bool StartCrashReporter(const std::filesystem::path& run_directory);
void CrashModule(const char* path,const char* phase,uint64_t memory_base=0,uint32_t image_base=0,uint32_t image_size=0);
bool CrashReporterSelfTest();
}
