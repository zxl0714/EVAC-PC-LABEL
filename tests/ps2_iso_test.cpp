#include "iso_disc.h"
#include "ps2_iso_fixture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::optional<cave::ps2::IsoFile> Find(const std::string& bytes,std::string_view path) {
  std::istringstream input(bytes);return cave::ps2::FindIsoFile(input,bytes.size(),path);
}
}
int main(int argc,char** argv) {
  try {
    const auto bytes=ps2_iso_fixture::Image();
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
      if(failure==2)ps2_iso_fixture::Pair(invalid,16*2048+156+2,24,4);
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
