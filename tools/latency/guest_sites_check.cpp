#include "guest_sites.h"
#include <iostream>

int main() {
  unsigned checks = 0;
  auto require = [&](bool value) {
    ++checks;
    if (!value) throw std::runtime_error("Guest-site check failed");
  };
  auto reject = [&](const std::string& text) {
    std::istringstream input(text);
    bool rejected = false;
    try { evac_latency::ReadGuestSites(input); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
  };
  std::istringstream input("# fixture only\n\n test.xex 1000 60000000 1 3 4 32 33\r\n");
  auto sites = evac_latency::ReadGuestSites(input);
  require(sites.size() == 1);
  require(sites[0].address == 0x1000 && sites[0].opcode == 0x60000000);
  require(sites[0].registers[2] == 32 && sites[0].registers[3] == 33);
  reject("");
  reject("test.xex 1000 60000000 0 3 4 32 33");
  reject("test.xex 1001 60000000 1 3 4 32 33");
  reject("test.xex 1000 60000000 1 3 4 32 34");
  reject("../test.xex 1000 60000000 1 3 4 32 33");
  reject("test.xex 1000 60000000 1 3 4 32 33 extra");
  reject("test.xex 1000 60000000 1 3 4 32");
  reject("test.xex 100000000 60000000 1 3 4 32 33");
  reject("test.xex 1000 60000000 1 3 4 32 33\ntest.xex 1004 60000000 1 3 4 32 33");
  reject("test.xex 1000 60000000 1 3 4 32 33\ntest.xex 1000 60000000 2 3 4 32 33");
  std::string bounded;
  for (unsigned n = 0; n < 257; ++n) {
    std::ostringstream line;
    line << "test.xex " << std::hex << (0x1000 + 4 * n) << " 60000000 " << std::dec
         << (n + 1) << " 3 4 32 33\n";
    bounded += line.str();
  }
  reject(bounded);
  std::cout << checks << " guest-site parser checks passed\n";
}
