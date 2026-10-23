#pragma once
#include <string>
#include <string_view>

namespace cave {
// Shared with the native adapters so launcher previews use the same spelling,
// byte limits and stable alias as the name actually sent to the original UI.
std::string NormalizeDisplayNickname(std::string_view name);
std::string LegacyDisplayNickname(std::string_view canonical_name);
std::string Ps4DisplayNickname(std::string_view canonical_name);
}
