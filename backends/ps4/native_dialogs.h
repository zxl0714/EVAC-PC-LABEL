#pragma once
#include <cstdint>
#include <string_view>

std::uint64_t ResolveDialogImport(std::string_view identifier, std::string_view library,
                                  std::string_view module);
bool DialogSelfTest();
bool ProfileDialogSelfTest();
int DialogDemo(std::wstring_view kind);
