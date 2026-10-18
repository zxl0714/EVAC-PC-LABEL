// Nickname utility retained from Futari 7dced597; HTTP is shared with G2.
#include "community_client.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <stdexcept>

bool FutariValidNickname(std::string_view name) { return cave::community::ValidNickname(name); }
std::string FutariNormalizeNickname(std::string_view name) { return cave::community::NormalizeNickname(name); }
std::string FutariRandomNickname() {
  static constexpr std::array first{"琥珀", "月光", "青空", "星河", "珊瑚", "雪原", "薄荷", "流星"};
  static constexpr std::array second{"水母", "海燕", "飞鱼", "萤火", "蜻蜓", "白鹭", "星蝶", "风铃"};
  static constexpr std::string_view alphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
  std::array<unsigned char, 6> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("Nickname random source unavailable");
  std::string result = std::string(first[bytes[0] % first.size()]) + second[bytes[1] % second.size()] + "-";
  for (size_t i = 2; i < bytes.size(); ++i) result += alphabet[bytes[i] % alphabet.size()];
  return result;
}
