#pragma once

#include <cstdint>
#include <string>

namespace ps2_iso_fixture {
inline void Pair(std::string& bytes,size_t offset,uint32_t value,unsigned count) {
  for(unsigned i=0;i<count;++i) {
    bytes.at(offset+i)=char(value>>(8*i));
    bytes.at(offset+count+i)=char(value>>(8*(count-1-i)));
  }
}
inline size_t Record(std::string& bytes,size_t offset,std::string name,uint32_t lba,uint32_t size,bool directory) {
  const auto length=(33+name.size()+1)&~size_t(1);
  bytes.at(offset)=char(length);Pair(bytes,offset+2,lba,4);Pair(bytes,offset+10,size,4);
  bytes.at(offset+25)=directory?2:0;Pair(bytes,offset+28,1,2);
  bytes.at(offset+32)=char(name.size());bytes.replace(offset+33,name.size(),name);
  return offset+length;
}
inline std::string Image() {
  std::string bytes(24*2048,0);
  const size_t primary=16*2048,terminator=17*2048;
  bytes[primary]=1;bytes.replace(primary+1,6,"CD001\1");
  bytes[terminator]=char(255);bytes.replace(terminator+1,6,"CD001\1");
  Pair(bytes,primary+80,24,4);Pair(bytes,primary+128,2048,2);
  Record(bytes,primary+156,std::string(1,0),20,2048,true);
  auto cursor=Record(bytes,20*2048,std::string(1,0),20,2048,true);
  cursor=Record(bytes,cursor,std::string(1,1),20,2048,true);
  Record(bytes,cursor,"DATA",21,2048,true);
  cursor=Record(bytes,21*2048,std::string(1,0),21,2048,true);
  cursor=Record(bytes,cursor,std::string(1,1),20,2048,true);
  Record(bytes,cursor,"BANK.;1",22,8,false);
  bytes.replace(22*2048,8,"fixture!");
  return bytes;
}
}
