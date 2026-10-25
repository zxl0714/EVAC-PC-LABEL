#include "common/stfs.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace cave;
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=5)throw std::runtime_error("Expected input, output, size, SHA256");
    const fs::path input=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
    const auto bytes=std::stoull(argv[3]);const auto hash=Utf8(argv[4]);
    const std::array files{DataFile{"0000000000000000/435A07D7/00000002/package/module/sample.bin",bytes,hash,false}};
    const ContentPackage package{"fixture","Fixture","0000000000000000/435A07D7/00000002/package","package",0x435A07D7,files};
    auto quick=InspectStfs(package,input,CheckLevel::Quick);
    if(quick.state!=DataState::Unverified || quick.hashes_checked)throw std::runtime_error("Quick recognition failed");
    auto full=InspectStfs(package,input,CheckLevel::Full);
    if(full.state!=DataState::Verified || full.container!=input || full.hashes_checked!=1)
      throw std::runtime_error("Full recognition rejected payload");
    StfsPackage archive(input,true);archive.Extract(output);
    if(fs::file_size(output/"module/sample.bin")!=bytes || Sha256(output/"module/sample.bin")!=hash)
      throw std::runtime_error("Extracted bytes differ");
    const std::array packages{package};
    const Game game{"fixture","Fixture","xbox360","unused","unused",true,false,{}, {},packages};
    auto discovered=InspectContent(game,input.parent_path(),CheckLevel::Full);
    if(discovered[0].state!=DataState::Verified || discovered[0].container!=input)
      throw std::runtime_error("Automatic container discovery failed");
    std::cout<<"Native DLC inspection and extraction passed\n";return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
