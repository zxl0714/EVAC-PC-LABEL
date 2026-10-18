#include <rex/system/time_zone.h>
#include <rex/ppc.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

REX_EXTERN(__imp__ExGetXConfigSetting);

using namespace rex::system;
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    TimeZoneSettings china;
    china.bias = -480;
    Require(EncodeTimeZoneSetting(china, 1) == std::array<uint8_t,4>{0xff,0xff,0xfe,0x20},
            "East-of-UTC bias must be signed, big-endian minutes");
    Require(EncodeTimeZoneSetting(china, 4) == std::array<uint8_t,4>{}, "No standard transition");
    Require(EncodeTimeZoneSetting(china, 5) == std::array<uint8_t,4>{}, "No daylight transition");
    TimeZoneSettings west;
    west.bias = 300;
    west.daylight_bias = -60;
    west.standard_date = {11,1,0,2};
    west.daylight_date = {3,2,0,2};
    Require(EncodeTimeZoneSetting(west, 1) == std::array<uint8_t,4>{0,0,1,0x2c}, "West bias sign");
    Require(EncodeTimeZoneSetting(west, 6) == std::array<uint8_t,4>{}, "Standard bias");
    Require(EncodeTimeZoneSetting(west, 7) == std::array<uint8_t,4>{0xff,0xff,0xff,0xc4}, "Daylight bias");
    Require(EncodeTimeZoneSetting(west, 4) == std::array<uint8_t,4>{11,1,0,2}, "Standard rule layout");
    Require(EncodeTimeZoneSetting(west, 5) == std::array<uint8_t,4>{3,2,0,2}, "Daylight rule layout");
    TimeZoneSettings half_hour;
    half_hour.bias = -330;
    Require(EncodeTimeZoneSetting(half_hour, 1) == std::array<uint8_t,4>{0xff,0xff,0xfe,0xb6}, "Non-whole-hour zone");
    Require(EncodeTimeZoneSetting(china, 2).has_value() && EncodeTimeZoneSetting(china, 3).has_value(), "Rule labels");
    Require(!EncodeTimeZoneSetting(china, 8) && !EncodeTimeZoneSetting(china, 0), "Unknown setting accepted");
    TIME_ZONE_INFORMATION host{};
    Require(GetTimeZoneInformation(&host)!=TIME_ZONE_ID_INVALID, "Host time zone query");
    TimeZoneSettings actual;
    actual.bias=host.Bias; actual.standard_bias=host.StandardBias; actual.daylight_bias=host.DaylightBias;
    actual.standard_date={uint8_t(host.StandardDate.wMonth),uint8_t(host.StandardDate.wDay),
        uint8_t(host.StandardDate.wDayOfWeek),uint8_t(host.StandardDate.wHour)};
    actual.daylight_date={uint8_t(host.DaylightDate.wMonth),uint8_t(host.DaylightDate.wDay),
        uint8_t(host.DaylightDate.wDayOfWeek),uint8_t(host.DaylightDate.wHour)};
    // Exercise the actual installed SDK import boundary, with a small authored
    // guest buffer. No Runtime, guest program or interactive subsystem is needed.
    alignas(32) std::array<uint8_t,256> buffer;
    auto query=[&](uint16_t setting,uint32_t destination,uint16_t capacity) {
      buffer.fill(0xcc);
      PPCContext ctx{};
      ctx.r3.u64=3;ctx.r4.u64=setting;ctx.r5.u64=destination;ctx.r6.u64=capacity;ctx.r7.u64=32;
      __imp__ExGetXConfigSetting(ctx,buffer.data());
      return ctx.r3.u32;
    };
    for(uint16_t setting=1;setting<=7;++setting) {
      Require(query(setting,64,4)==0,"SDK time zone import failed");
      const auto expected=EncodeTimeZoneSetting(actual,setting).value();
      Require(std::equal(expected.begin(),expected.end(),buffer.begin()+64),"SDK returned wrong host time zone field");
      Require(buffer[32]==0 && buffer[33]==4 && buffer[63]==0xcc && buffer[68]==0xcc,
              "SDK required size/endian/guard bytes");
    }
    Require(query(1,64,3)!=0 && buffer[64]==0xcc,"Small buffer must not be written");
    Require(query(1,0,0)==0 && buffer[32]==0 && buffer[33]==4,"Size-only query");
    std::cout << "Time zone encoding passed; no host setting or stored clock changed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
