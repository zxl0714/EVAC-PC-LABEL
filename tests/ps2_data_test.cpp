#include "common/catalogue.h"
#include "common/data_import.h"
#include "common/paths.h"
#include "common/ps2_prepare.h"
#include <Windows.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cave;
namespace {
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void Write(const fs::path& file,const std::string& bytes) {
  fs::create_directories(file.parent_path());
  std::ofstream output(file,std::ios::binary);output.write(bytes.data(),bytes.size());
  if(!output)throw std::runtime_error("Cannot write authored ISO fixture");
}
void Pair(std::string& bytes,size_t offset,uint32_t value,unsigned count) {
  for(unsigned i=0;i<count;++i) {
    bytes.at(offset+i)=char(value>>(8*i));
    bytes.at(offset+count+i)=char(value>>(8*(count-1-i)));
  }
}
size_t Record(std::string& bytes,size_t offset,const std::string& name,uint32_t lba,uint32_t size,bool directory) {
  const auto length=(33+name.size()+1)&~size_t(1);
  bytes.at(offset)=char(length);Pair(bytes,offset+2,lba,4);Pair(bytes,offset+10,size,4);
  bytes.at(offset+25)=directory?2:0;Pair(bytes,offset+28,1,2);
  bytes.at(offset+32)=char(name.size());bytes.replace(offset+33,name.size(),name);
  return offset+length;
}
std::string Iso() {
  std::string bytes(24*2048,0);
  const auto primary=16*2048,terminator=17*2048;
  bytes[primary]=1;bytes.replace(primary+1,6,"CD001\1");
  bytes[terminator]=char(255);bytes.replace(terminator+1,6,"CD001\1");
  Pair(bytes,primary+80,24,4);Pair(bytes,primary+128,2048,2);
  Record(bytes,primary+156,std::string(1,0),20,2048,true);
  auto cursor=Record(bytes,20*2048,std::string(1,0),20,2048,true);
  cursor=Record(bytes,cursor,std::string(1,1),20,2048,true);
  Record(bytes,cursor,"SLPS_253.52;1",22,3,false);
  bytes.replace(22*2048,3,"abc");return bytes;
}
struct Fixture {
  fs::path directory,root,data;
  Fixture() {
    const auto temporary=fs::canonical(fs::temp_directory_path());
    directory=temporary/(L"evac-ps2-data-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!fs::create_directory(directory))throw std::runtime_error("Fixture directory collision");
    root=directory/L"collection";data=directory/L"originals";
    fs::create_directory(root);fs::create_directory(data);
  }
  ~Fixture() {
    try {
      const auto actual=fs::canonical(directory),temporary=fs::canonical(fs::temp_directory_path());
      if(actual.parent_path()==temporary && actual.filename().native().starts_with(L"evac-ps2-data-"))fs::remove_all(actual);
    } catch(...) {}
  }
};
}
int main() {
  try {
    Fixture fixture;const auto bytes=Iso();
    const auto original=fixture.data/FromUtf8("Original name 空格.ISO");Write(original,bytes);
    const auto hash=Sha256(original);
    const std::array files{DataFile{"SLPS_253.52",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",true},
                          DataFile{"disc.iso",bytes.size(),hash,false}};
    Game game{"espgaluda","Authored ISO","ps2","game-data/espgaluda","runtime/fake.exe",true,false,files,{}};
    auto inspect=[&](CheckLevel level=CheckLevel::Full) {return Inspect(game,fixture.root,fixture.data,level);};
    auto result=inspect();
    Require(result.data_state==DataState::Verified && result.disc_image==original,"Original Unicode ISO name was not recognized");
    Require(result.hashes_checked==2 && result.layout=="iso","Boot and disc identities were not checked");
    Require(inspect(CheckLevel::Quick).data_state==DataState::Unverified,"Quick inspection falsely certified the entire disc");
    const std::array prepared{Ps2PreparedFile{"SLPS_253.52","boot.elf",3,files[0].sha256}};
    const auto paths=Paths(fixture.root,game.id,fixture.data);
    const auto cache=PreparePs2Disc(paths,original,hash,prepared);
    Require(Sha256(cache/"boot.elf")==files[0].sha256 && Sha256(original)==hash,"Prepared boot differs from the original ISO");
    Require(PreparePs2Disc(paths,original,hash,prepared)==cache,"Verified prepared cache was not reused");
    Write(cache/"unexpected.bin","stale override");
    Require(PreparePs2Disc(paths,original,hash,prepared)==cache && !fs::exists(cache/"unexpected.bin"),
            "Unexpected prepared resources were accepted");
    bool retained=false;
    for(const auto& entry:fs::directory_iterator(paths.prepared))
      if(entry.path().filename().native().starts_with(L"previous-") && fs::exists(entry.path()/"unexpected.bin"))retained=true;
    Require(retained,"Previous prepared cache was not preserved");
    auto changed=bytes;changed[23*2048]=1;Write(original,changed);
    Require(inspect(CheckLevel::Programs).data_state==DataState::Unverified,"Unchanged boot was rejected by program inspection");
    Require(inspect().data_state==DataState::Incomplete,"Same-size resource corruption passed full validation");
    changed=bytes;changed[22*2048]='x';Write(original,changed);
    Require(inspect(CheckLevel::Quick).data_state==DataState::Unsupported,"Quick inspection accepted a different boot program");
    Write(original,bytes);Write(fixture.data/"duplicate.iso",bytes);
    Require(inspect().data_state==DataState::Unknown && inspect().disc_image.empty(),"Ambiguous ISO copies silently selected");
    fs::rename(fixture.data/"duplicate.iso",fixture.data/"duplicate.saved");
    Write(fixture.data/"unrelated.iso","not an ISO");Write(fixture.data/"private.txt","unrelated original data");
    Require(inspect().data_state==DataState::Verified,"Unrelated ROMs prevented selection of the supported game");
    const auto copied=CopyDataToCollection(fixture.root,game,fixture.data);
    Require(copied.files==1 && copied.bytes==bytes.size(),"PS2 import copied unrelated files from a shared ROM folder");
    Require(fs::exists(copied.directory/original.filename()) && !fs::exists(copied.directory/"private.txt") &&
            !fs::exists(copied.directory/"unrelated.iso"),"PS2 import changed the ISO name or copied other source content");
    Require(Sha256(original)==hash && fs::file_size(fixture.data/"private.txt")==23,"Import modified original data");
    changed=bytes;Pair(changed,16*2048+156+2,24,4);Write(original,changed);
    Require(inspect().data_state==DataState::Unsupported,"Out-of-bounds ISO extent was accepted");
    bool rejected=false;try {Sha256Range(original,bytes.size()-1,2);}catch(const std::exception&){rejected=true;}
    Require(rejected,"Out-of-bounds checksum range was accepted");
    std::cout<<"PS2 original ISO recognition, boot/disc validation, ambiguity and isolated import passed\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
