#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include "storage/save_store.h"

void ConfigureNativeStorage(const std::filesystem::path& user_directory);
std::filesystem::path NativeStorageRoot();
std::uint64_t ResolveStorageImport(std::string_view identifier, std::string_view library,
                                   std::string_view module);
bool StorageSelfTest(const std::filesystem::path& directory);
bool SaveStoreSelfTest(const std::filesystem::path& directory);
bool SaveCrashFixture(const std::filesystem::path& directory, std::string_view phase);
bool NativeSavePath(std::string_view path);
std::unique_ptr<kt::save::File> OpenNativeSaveFile(std::string_view path, kt::save::OpenOptions options);
