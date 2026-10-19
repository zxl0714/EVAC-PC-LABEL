#pragma once

// Optional out-of-process CPU diagnostics for the probe, not a game runtime.
int SampleNativeProcess(unsigned process_id);
int TraceNativeProcess(const wchar_t* executable, const wchar_t* input, const wchar_t* user_directory = nullptr,
                       const wchar_t* module_directory = nullptr, const wchar_t* app_directory = nullptr,
                       const wchar_t* patch_directory = nullptr, const wchar_t* addons_directory = nullptr);
