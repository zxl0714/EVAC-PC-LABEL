#include "iso_disc.h"
#include "iso_read_ahead.h"
#include "ps2_iso_fixture.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <share.h>
#endif

namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::optional<cave::ps2::IsoFile> Find(const std::string& bytes,std::string_view path) {
  std::istringstream input(bytes);return cave::ps2::FindIsoFile(input,bytes.size(),path);
}
void CheckReadAhead() {
  constexpr size_t sector = 2048, block = 256 * 1024;
  std::vector<unsigned char> expected(5 * block + 173);
  uint32_t random = 123;
  for (auto& byte : expected) {
    random = random * 1664525u + 1013904223u;
    byte = static_cast<unsigned char>(random >> 24);
  }
  const auto path = std::filesystem::temp_directory_path() /
      ("evac-disc-read-ahead-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
#ifdef _WIN32
  std::FILE* temporary = _wfsopen(path.c_str(), L"w+bx", _SH_DENYNO);
#else
  std::FILE* temporary = std::fopen(path.c_str(), "w+bx");
#endif
  auto closeFile = [&](std::FILE* stream) {
    std::fclose(stream);
    std::error_code error;
    std::filesystem::remove(path, error);
  };
  std::unique_ptr<std::FILE, decltype(closeFile)> file(temporary, closeFile);
  Require(file && std::fwrite(expected.data(), 1, expected.size(), file.get()) == expected.size(),
          "Unable to create disc fixture");
  std::rewind(file.get());
  auto reader = cave::ps2::IsoReadAhead::Open(file.get(), expected.size());
  Require(bool(reader), "Cannot open read-ahead for the disc fixture");
  std::array<unsigned char, sector> result{};
  auto expectCached = [&](size_t offset, size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!reader->TryRead(offset, result.data(), count)) {
      Require(std::chrono::steady_clock::now() < deadline, "Sequential disc data was never prefetched");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(std::equal(result.begin(), result.begin() + count, expected.begin() + offset),
            "Read-ahead returned bytes from a different sector");
  };
  for (size_t start : {size_t(0), 4 * block, 2 * block, size_t(0)}) {
    reader->TryRead(start, result.data(), sector);
    reader->TryRead(start + sector, result.data(), sector);
    expectCached(start + sector, sector);
    expectCached(start + 2 * sector, sector);
    const size_t next = start + block;
    const size_t count = std::min(sector, expected.size() - next);
    expectCached(next, count);
  }
  Require(!reader->TryRead(expected.size() - 1, result.data(), 2), "Read past the image boundary");
  Require(std::ftell(file.get()) == 0 && std::fgetc(file.get()) == expected[0],
          "Background reads moved the foreground file position");
}
}
int main(int argc,char** argv) {
  try {
    CheckReadAhead();
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
