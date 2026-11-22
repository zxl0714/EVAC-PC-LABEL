#pragma once
#include <cstdint>
#include <atomic>
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
  // The enclosing range can contain gaps. A hit still requires a live module header.
  bool mayContain(uint32_t address) const noexcept {
    return address>=firstAddress_ && address<lastAddress_;
  }
  const StaticModule* selected(uint32_t address) const;
  Function lookup(uint32_t address) const;
  void clear();
private:
  std::span<const uint8_t> memory_;
  std::vector<StaticModule> modules_;
  // Distinct, non-overlapping banks allow a validated last-hit shortcut.
  // The loader can replace a bank at any instruction, so always check its header.
  mutable std::atomic<uint32_t> lastMatch_{UINT32_MAX};
  bool disjointBases_=true;
  uint32_t firstAddress_=UINT32_MAX,lastAddress_=0;
};
}
