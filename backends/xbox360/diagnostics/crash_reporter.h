#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
namespace cave::xbox360 {
bool StartCrashReporter(const std::filesystem::path& run_directory,std::string_view game,std::string_view build);
void CrashModule(const char* path,const char* phase,uint64_t memory_base=0,uint32_t image_base=0,uint32_t image_size=0);
bool CrashReporterSelfTest();
}
