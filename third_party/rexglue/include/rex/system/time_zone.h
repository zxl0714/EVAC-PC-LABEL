#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace rex::system {

// Xbox XConfig uses four-byte transition rules, not a host SYSTEMTIME:
// month, week-of-month (5 = last), weekday (Sunday = 0), hour.
struct TimeZoneTransition {
  uint8_t month = 0, week = 0, weekday = 0, hour = 0;
};
struct TimeZoneSettings {
  // Minutes added to local time to obtain UTC, matching the native convention.
  int32_t bias = 0, standard_bias = 0, daylight_bias = 0;
  TimeZoneTransition standard_date, daylight_date;
};

inline std::optional<std::array<uint8_t, 4>> EncodeTimeZoneSetting(
    const TimeZoneSettings& zone, uint16_t setting) {
  auto number = [](int32_t value) -> std::array<uint8_t, 4> {
    const auto bits = static_cast<uint32_t>(value);
    return {uint8_t(bits >> 24), uint8_t(bits >> 16), uint8_t(bits >> 8), uint8_t(bits)};
  };
  auto transition = [](TimeZoneTransition date) -> std::array<uint8_t, 4> {
    return {date.month, date.week, date.weekday, date.hour};
  };
  switch (setting) {
    case 1: return number(zone.bias);
    // These four-byte labels identify standard/daylight rules. Full localized
    // Windows names do not fit this Xbox field and are not used for conversion.
    case 2: return std::array<uint8_t, 4>{'S', 'T', 'D', 0};
    case 3: return std::array<uint8_t, 4>{'D', 'L', 'T', 0};
    case 4: return transition(zone.standard_date);
    case 5: return transition(zone.daylight_date);
    case 6: return number(zone.standard_bias);
    case 7: return number(zone.daylight_bias);
    default: return std::nullopt;
  }
}
}  // namespace rex::system
