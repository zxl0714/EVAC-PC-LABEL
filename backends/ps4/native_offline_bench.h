#pragma once
#include <filesystem>

// Authored workloads only: no guest entry, ROM, window or display-clock change.
int RunNativeOfflineBench(const std::filesystem::path& library, unsigned samples, unsigned warmup,
                         bool pin_workers = false);
int RunNativeIndexedDrawSelfTest(const std::filesystem::path& library, bool validation = false);
