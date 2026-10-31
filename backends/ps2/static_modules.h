#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct R5900Context;
class PS2Runtime;

namespace cave::ps2 {
using Function=void(*)(uint8_t*,R5900Context*,PS2Runtime*);

struct StaticModule {
  uint32_t id{},address{},textSize{},dataSize{},bssSize{},tableAddress{};
  std::span<const Function> table;
};

// Each runtime owns its immutable module descriptions. The original loader
// writes the MWo3 header; its identity selects the matching native code bank.
class StaticModules {
public:
  void add(std::span<const uint8_t> memory,const StaticModule& module);
  const StaticModule* selected(uint32_t address) const;
  Function lookup(uint32_t address) const;
  void clear();
private:
  std::span<const uint8_t> memory_;
  std::vector<StaticModule> modules_;
};
}
