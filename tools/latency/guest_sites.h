#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace xe::cpu { class Function; }
namespace evac_latency {

struct GuestSite {
  std::string module;
  uint32_t address = 0;
  uint32_t opcode = 0;
  uint32_t id = 0;
  std::array<unsigned, 4> registers{};
  xe::cpu::Function* function = nullptr;
};

inline std::vector<GuestSite> ReadGuestSites(std::istream& input) {
  std::vector<GuestSite> sites;
  std::unordered_set<uint32_t> ids;
  std::unordered_set<std::string> locations;
  std::string line;
  while (std::getline(input, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') continue;
    GuestSite site;
    std::istringstream row(line);
    std::string extra;
    if (!(row >> site.module >> std::hex >> site.address >> site.opcode >> std::dec >> site.id
              >> site.registers[0] >> site.registers[1] >> site.registers[2] >> site.registers[3]) ||
        (row >> extra) || !site.address || site.address % 4 || !site.id ||
        site.module.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") != std::string::npos ||
        !ids.insert(site.id).second ||
        !locations.insert(site.module + ":" + std::to_string(site.address)).second ||
        sites.size() >= 256) {
      throw std::runtime_error("Invalid or duplicate latency guest observation site");
    }
    for (const auto reg : site.registers) {
      if (reg > 33) throw std::runtime_error("Latency observation register must be 0..33");
    }
    sites.push_back(std::move(site));
  }
  if (sites.empty()) throw std::runtime_error("Empty latency guest site file");
  return sites;
}

}  // namespace evac_latency
