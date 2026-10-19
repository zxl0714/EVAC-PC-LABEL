#pragma once
#include "content/content_view.h"

enum class ContentModuleAction { Load, Unload, IsLoaded };
void ConfigureNativeContent(const std::filesystem::path& app, const std::filesystem::path& patch = {}, const std::filesystem::path& addons = {}, const std::filesystem::path& user_directory = {}, const std::filesystem::path& read_patches = {}, bool merged_patch = false);
bool NativeContentConfigured();
std::string NativeContentTitleId();
int ContentModule(ContentModuleAction action, const void* caller = nullptr);
std::uint64_t ResolveContentImport(std::string_view identifier, std::string_view library, std::string_view module);
std::unique_ptr<kt::content::ReadOnlyFile> OpenNativeContentFile(std::string_view virtual_path, bool allow_directory = false);
bool ContentSelfTest(const std::filesystem::path& app, const std::filesystem::path& patch, const std::filesystem::path& addons);
