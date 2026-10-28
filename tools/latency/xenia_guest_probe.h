#pragma once

#include "latency_probe.h"
#include "guest_sites.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include "xenia/base/logging.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

namespace evac_latency {

inline void ObserveGuest(xe::cpu::ppc::PPCContext* context, void* arg0, void*) {
  const auto& site = *static_cast<const GuestSite*>(arg0);
  auto value = [&](unsigned reg) -> uint64_t {
    return reg < 32 ? context->r[reg] : reg == 32 ? context->lr : context->ctr;
  };
  Record(Kind::GuestObservation, site.id, site.address,
         value(site.registers[0]), value(site.registers[1]),
         value(site.registers[2]), value(site.registers[3]));
}

// The descriptors and builtin arguments stay alive for the process lifetime.
// Only matching module/address/opcode sites emit callbacks; no guest memory is
// written or read, no breakpoint is installed, and no game thread is suspended.
class XeniaGuestProbe {
 public:
  void Initialize(xe::cpu::Processor* processor) {
    if (!state) return;
    wchar_t path[32768];
    const auto length = GetEnvironmentVariableW(L"EVAC_LATENCY_GUEST_SITES", path, 32768);
    if (!length) return;
    if (length >= 32768) throw std::runtime_error("Guest site path too long");
    std::ifstream input{std::filesystem::path(path)};
    if (!input) throw std::runtime_error("Cannot open latency guest sites");
    auto sites = ReadGuestSites(input);
    for (auto& site : sites) {
      auto* stored = new GuestSite(std::move(site));
      stored->function = processor->DefineBuiltin(
          "LatencyGuestObservation_" + std::to_string(stored->id), ObserveGuest, stored, nullptr);
      sites_[stored->address].push_back(stored);
      XELOGI("Latency observation {} {} {:08X} opcode {:08X} registers {},{},{},{}",
             stored->id, stored->module, stored->address, stored->opcode,
             stored->registers[0], stored->registers[1], stored->registers[2], stored->registers[3]);
    }
  }

  xe::cpu::Function* Find(const std::string& module, uint32_t address, uint32_t opcode) const {
    const auto found = sites_.find(address);
    if (found == sites_.end()) return nullptr;
    for (const auto* site : found->second) {
      if (site->module != module) continue;
      if (site->opcode != opcode) {
        Record(Kind::GuestObservation, 0, address, site->opcode, opcode, site->id);
        XELOGE("Latency observation opcode mismatch: {} {:08X}", module, address);
        return nullptr;
      }
      return site->function;
    }
    return nullptr;
  }

 private:
  std::unordered_map<uint32_t, std::vector<GuestSite*>> sites_;
};

}  // namespace evac_latency
