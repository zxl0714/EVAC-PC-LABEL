#pragma once
#include <cstdint>
#include <string_view>

enum class Ngs2ModuleAction { Load, Unload, IsLoaded };
int Ngs2Module(Ngs2ModuleAction action);
std::uint64_t ResolveNgs2Import(std::string_view identifier, std::string_view library, std::string_view module);
bool Ngs2SelfTest();
int Ngs2WaveformCheck(const wchar_t* path);
