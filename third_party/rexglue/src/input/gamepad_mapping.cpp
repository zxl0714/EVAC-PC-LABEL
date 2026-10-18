#include <rex/input/gamepad_mapping.h>
#include <rex/cvar.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <string_view>

#define PAD_BIND(name, def) REXCVAR_DEFINE_STRING(pad_##name, def, "Input/Gamepad", "Physical source for Xbox " #name)
PAD_BIND(up, "UP"); PAD_BIND(down, "DOWN"); PAD_BIND(left, "LEFT"); PAD_BIND(right, "RIGHT");
PAD_BIND(start, "START"); PAD_BIND(back, "BACK"); PAD_BIND(ls, "LS"); PAD_BIND(rs, "RS");
PAD_BIND(lb, "LB"); PAD_BIND(rb, "RB"); PAD_BIND(a, "A"); PAD_BIND(b, "B");
PAD_BIND(x, "X"); PAD_BIND(y, "Y"); PAD_BIND(lt, "LT"); PAD_BIND(rt, "RT");
#undef PAD_BIND

namespace rex::input {
namespace {
constexpr std::array<std::string_view, 17> sources{
  "UP","DOWN","LEFT","RIGHT","START","BACK","LS","RS","LB","RB","A","B","X","Y","LT","RT","NONE"};
constexpr std::array<uint16_t, 14> buttons{
  0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,0x0100,0x0200,0x1000,0x2000,0x4000,0x8000};
constexpr std::array<std::string_view, 16> flags{
  "pad_up","pad_down","pad_left","pad_right","pad_start","pad_back","pad_ls","pad_rs",
  "pad_lb","pad_rb","pad_a","pad_b","pad_x","pad_y","pad_lt","pad_rt"};
struct Mapping {
  std::mutex mutex;
  std::array<unsigned, 16> source{};
  bool identity = true;
  Mapping() {
    for (unsigned i = 0; i < flags.size(); ++i) {
      Set(i, cvar::GetFlagByName(flags[i]));
      cvar::RegisterChangeCallback(flags[i], [this, i](auto, auto value) { Set(i, value); });
    }
  }
  void Set(unsigned destination, std::string_view value) {
    std::lock_guard lock(mutex);
    auto it = std::find(sources.begin(), sources.end(), value);
    source[destination] = it == sources.end() ? 16 : unsigned(it - sources.begin());
    identity = true;
    for (unsigned i = 0; i < source.size(); ++i) identity &= source[i] == i;
  }
};
}

void RemapPhysicalGamepad(X_INPUT_GAMEPAD& pad) {
  // The registry owns callbacks through process shutdown.
  static auto* mapping = new Mapping;
  std::array<unsigned, 16> map;
  {
    std::lock_guard lock(mapping->mutex);
    if (mapping->identity) return;
    map = mapping->source;
  }
  const auto original = pad;
  auto strength = [&](unsigned source) -> uint8_t {
    if (source < buttons.size()) return (uint16_t(original.buttons) & buttons[source]) ? 255 : 0;
    if (source == 14) return original.left_trigger;
    if (source == 15) return original.right_trigger;
    return 0;
  };
  // Preserve guide/reserved bits and analog sticks, including stick movement
  // on devices without a D-pad. All ordinary buttons/triggers use the map.
  uint16_t output = uint16_t(original.buttons) & 0x0c00;
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (strength(map[i]) >= 30) output |= buttons[i];
  pad.buttons = output;
  pad.left_trigger = strength(map[14]);
  pad.right_trigger = strength(map[15]);
}
}
