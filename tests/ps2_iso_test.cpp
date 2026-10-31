#include "iso_disc.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void Pair(std::string& bytes,size_t offset,uint32_t value,unsigned count) {
  for(unsigned i=0;i<count;++i) {
    bytes.at(offset+i)=char(value>>(8*i));
    bytes.at(offset+count+i)=char(value>>(8*(count-1-i)));
  }
}
size_t Record(std::string& bytes,size_t offset,std::string name,uint32_t lba,uint32_t size,bool directory) {
  const auto length=(33+name.size()+1)&~size_t(1);
  bytes.at(offset)=char(length);Pair(bytes,offset+2,lba,4);Pair(bytes,offset+10,size,4);
  bytes.at(offset+25)=directory?2:0;Pair(bytes,offset+28,1,2);
  bytes.at(offset+32)=char(name.size());bytes.replace(offset+33,name.size(),name);
  return offset+length;
}
std::string Fixture() {
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
std::optional<cave::ps2::IsoFile> Find(const std::string& bytes,std::string_view path) {
  std::istringstream input(bytes);return cave::ps2::FindIsoFile(input,bytes.size(),path);
}
}
int main(int argc,char** argv) {
  try {
    const auto bytes=Fixture();
    for(std::string_view path:{"cdrom0:\\data\\bank.;1","DATA/BANK","/data/bank.;1"}) {
      const auto file=Find(bytes,path);
      Require(file && file->lba==22 && file->size==8,"Original ISO extent was not retained");
    }
    Require(!Find(bytes,"DATA/OTHER") && !Find(bytes,"DATA"),"Missing file or directory returned as file");
    for(std::string_view path:{"../BANK","DATA/../BANK","DATA//BANK","DATA/BANK;X"}) {
      bool rejected=false;try {Find(bytes,path);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Invalid disc path was accepted");
    }
    for(unsigned failure=0;failure<3;++failure) {
      auto invalid=bytes;
      if(failure==0)invalid[16*2048+84]^=1;
      if(failure==1)invalid.resize(23*2048);
      if(failure==2)Pair(invalid,16*2048+156+2,24,4);
      bool rejected=false;try {Find(invalid,"DATA/BANK");}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Invalid ISO extent or endian fields were accepted");
    }
    if(argc==5) {
      std::ifstream image(argv[1],std::ios::binary);
      const auto file=cave::ps2::FindIsoFile(image,std::filesystem::file_size(argv[1]),argv[2]);
      Require(file && file->lba==std::stoul(argv[3],nullptr,0) && file->size==std::stoul(argv[4],nullptr,0),
              "Read-only disc inventory disagrees with expected original extent");
    }else if(argc!=1)throw std::runtime_error("Expected optional ISO, path, LBA and size");
    std::cout<<"PS2 ISO lookup, original LBA, filename normalization and extent checks passed\n";
    return 0;
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
