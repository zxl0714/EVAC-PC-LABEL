#include "static_modules.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace cave::ps2 {
void StaticModules::add(std::span<const uint8_t> memory,const StaticModule& module) {
  const uint64_t textBegin=uint64_t(module.address)+64;
  const uint64_t textEnd=textBegin+module.textSize;
  const uint64_t memoryEnd=textEnd+module.dataSize+module.bssSize;
  const uint64_t tableEnd=uint64_t(module.tableAddress)+module.table.size()*4;
  if(!memory.data() || memory.empty() || memoryEnd>memory.size() || memory.size()>0x2000000 ||
     ((module.address|module.textSize|module.dataSize|module.bssSize|module.tableAddress)&3) ||
     !module.textSize || module.table.empty() || module.table.size()>module.textSize/4 ||
     module.tableAddress<textBegin ||
     tableEnd>textEnd || modules_.size()>=32 ||
     (!memory_.empty() && (memory_.data()!=memory.data() || memory_.size()!=memory.size())))
    throw std::invalid_argument("Invalid static PS2 module extent or table");
  for(const auto& existing:modules_)
    if(existing.address==module.address && existing.id==module.id)
      throw std::invalid_argument("Duplicate static PS2 module identity");
  memory_=memory;
  modules_.push_back(module);
}

const StaticModule* StaticModules::selected(uint32_t address) const {
  if(address&3)return nullptr;
  for(const auto& module:modules_) {
    const uint64_t textBegin=uint64_t(module.address)+64;
    if(address<textBegin || address>=textBegin+module.textSize)continue;
    std::array<uint32_t,6> header{};
    std::memcpy(header.data(),memory_.data()+module.address,sizeof(header));
    if(header[0]!=0x336f574d || header[1]!=module.id || header[2]!=module.address ||
       header[3]!=module.textSize || header[4]!=module.dataSize || header[5]!=module.bssSize)
      continue;
    return &module;
  }
  return nullptr;
}

Function StaticModules::lookup(uint32_t address) const {
  const auto* module=selected(address);
  if(!module || address<module->tableAddress)return nullptr;
  const uint32_t slot=(address-module->tableAddress)/4;
  return slot<module->table.size()?module->table[slot]:nullptr;
}

void StaticModules::clear() {modules_.clear();memory_={};}
}
