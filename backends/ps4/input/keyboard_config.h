#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace kt::input {
using KeyboardBindings = std::array<std::uint32_t, 256>;
KeyboardBindings DefaultKeyboardBindings();
// Partial [keyboard] entries replace that button's complete list of keys.
// Parse into a new value so a malformed file cannot partially change input.
KeyboardBindings ParseKeyboardConfig(std::string_view text);
KeyboardBindings LoadKeyboardConfig(const std::filesystem::path& path);
}
