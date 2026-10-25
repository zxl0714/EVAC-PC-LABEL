#include "diagnostics/content_prepare.h"
#include <rex/system/xam/content_manager.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cave;
namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void Write(const fs::path& path,std::string_view text="abc") {
  fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<text;
  if(!output)throw std::runtime_error("fixture write");
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    Require(argc==2,"Expected fresh fixture directory");
    const auto root=fs::absolute(argv[1]);Require(!fs::exists(root),"Fixture already exists");
    fs::create_directories(root);
    const auto paths=Paths(root,"fixture",root/"game-data/fixture");
    const auto source=paths.data/"dlc";
    constexpr auto hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::array a{DataFile{"0000000000000000/435A07D7/00000002/FIRST/asset.bin",3,hash,false}};
    const std::array b{DataFile{"0000000000000000/435A07D7/00000002/SECOND/key.bin",3,hash,false}};
    const std::array packages{ContentPackage{"first","First DLC","0000000000000000/435A07D7/00000002/FIRST","FIRST",0x435A07D7,a,"First native identity"},
      ContentPackage{"second","Second DLC","0000000000000000/435A07D7/00000002/SECOND","SECOND",0x435A07D7,b}};
    const Game game{"fixture","Fixture","xbox360","game-data/fixture","runtime/fake.exe",true,false,{}, {},packages};
    auto prepare=[&] {return xbox360::PrepareContent(game,paths,source,InspectContent(game,source,CheckLevel::Full));};
    auto empty=prepare();Require(fs::is_directory(empty),"No-DLC cache unavailable");
    Write(source/"00000002/FIRST/asset.bin");
    const auto one=prepare();Require(one!=empty && Within(one,paths.prepared),"Cache escaped preparation directory");
    const auto header=one/"0000000000000000/435A07D7/Headers/00000002/FIRST.header";
    Require(fs::file_size(header)==0x148,"Generated header size");
    using namespace rex::system;
    xam::ContentManager manager(nullptr,paths.profile);manager.SetReadOnlyContentRoot(one);
    const auto list=manager.ListContent(1,0,XContentType::kMarketplaceContent,0x435A07D7);
    Require(list.size()==1 && list[0].file_name()=="FIRST" && list[0].display_name()==u"First native identity","Prepared DLC enumeration/native identity");
    Require(FindGame("futari").content[0].native_name=="虫姫さまふたり　ブラックレーベル" &&
      FindGame("futari").content[1].native_name=="虫姫さまふたり　Ver1.01","Futari native DLC names changed");
    const auto stamp=fs::last_write_time(header);
    Require(prepare()==one && fs::last_write_time(header)==stamp,"Valid cache rewritten");
    Write(header,std::string(0x148,'x'));
    Require(prepare()==one && fs::file_size(header)==0x148,"Damaged header not repaired");
    Write(source/"00000002/SECOND/key.bin","bad");
    Require(prepare()==one,"Corrupt sibling disabled valid DLC");
    Write(source/"00000002/SECOND/key.bin");
    const auto two=prepare();Require(two!=one,"Package selection not part of cache identity");
    const auto checks=InspectContent(game,two,CheckLevel::Full);
    Require(checks[0].state==DataState::Verified && checks[1].state==DataState::Verified,"Both packages not prepared");
    fs::remove(source/"00000002/FIRST/asset.bin");
    const auto second_only=prepare();Require(second_only!=two && second_only!=one,"Removed source remained mounted");
    const auto single=InspectContent(game,second_only,CheckLevel::Full);
    Require(single[0].state==DataState::Missing && single[1].state==DataState::Verified,"Second-only mount");
    for(const auto& entry:fs::recursive_directory_iterator(source))
      if(entry.is_regular_file())Require(entry.path().filename()=="key.bin" && Sha256(entry.path())==hash,"Original DLC changed");
    std::cout<<"Generated DLC cache: none, singles, both, reuse, repair and source preservation passed\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
