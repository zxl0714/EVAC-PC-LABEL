#include "common/daioujou_music.h"
#include "common/verification_cache.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace cave::daioujou {
namespace {
constexpr std::array kCues{
  ArrangeCue{"ddo_arr_aendg_full",7206016,299220,6884745},
  ArrangeCue{"ddo_arr_aendg_short",2760384,190328,2543287},
  ArrangeCue{"ddo_arr_boss",2706180,114146,2592000},
  ArrangeCue{"ddo_arr_govr",230712,0,0},
  ArrangeCue{"ddo_arr_lbs1",5396590,276567,5120000},
  ArrangeCue{"ddo_arr_lbs3",4306428,301818,4004571},
  ArrangeCue{"ddo_arr_name",4019295,140067,3879184},
  ArrangeCue{"ddo_arr_psel",2805362,74682,2730668},
  ArrangeCue{"ddo_arr_sclr",2877664,1699466,1178182},
  ArrangeCue{"ddo_arr_stg1",7293360,125322,7168000},
  ArrangeCue{"ddo_arr_stg2",6490722,568372,5922305},
  ArrangeCue{"ddo_arr_stg3",5415511,223247,5192241},
  ArrangeCue{"ddo_arr_stg4",10533067,32737,10500309},
  ArrangeCue{"ddo_arr_stg5",7365956,2425013,4940923},
};
constexpr std::array kBanks{
  ArrangeBank{"ddo_bgm_SoundBank.xsb",2737,"6513daa1a4db3aacfa4b32cba6d862d40f5365bf507eee47c6a2413176ada5fc"},
  ArrangeBank{"ddo_bgm_WaveBank.xwb",736970752,"c89d558adb9fced6b868ddcb19e73d554feb98988f129ae17915de8fa434b6f0"},
};
struct Extent {uint64_t offset{},bytes{};};

void Require(bool valid,const char* message) {if(!valid)throw std::runtime_error(message);}
uint32_t ReadNumber(std::span<const unsigned char> data,size_t at,unsigned size,bool big=false) {
  Require(size<=4 && at<=data.size() && size<=data.size()-at,"Invalid X Arrange bank offset");
  uint32_t value{};
  for(unsigned n=0;n<size;++n)value|=uint32_t(data[at+n])<<(8*(big?size-n-1:n));
  return value;
}
bool Magic(std::span<const unsigned char> data,std::string_view magic) {
  return data.size()>=magic.size() && std::equal(magic.begin(),magic.end(),data.begin());
}
// A write/delete-denying handle stays open through validation and reading.
class BankLock {
 public:
  explicit BankLock(const fs::path& path):handle_(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,
      nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)) {
    Require(handle_!=INVALID_HANDLE_VALUE,"Cannot read X Arrange music bank");
  }
  ~BankLock(){CloseHandle(handle_);}
  BankLock(const BankLock&)=delete;
 private: HANDLE handle_;
};
class WaveBank {
 public:
  explicit WaveBank(const fs::path& path):input_(path,std::ios::binary) {
    Require(bool(input_),"Cannot open X Arrange wave bank");
  }
  void Read(uint64_t at,std::span<unsigned char> bytes) {
    Require(at<=kBanks[1].bytes && bytes.size()<=kBanks[1].bytes-at,"Invalid X Arrange bank extent");
    input_.seekg(static_cast<std::streamoff>(at));
    Require(bool(input_.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))),
            "Cannot read X Arrange wave bank");
  }
 private: std::ifstream input_;
};

void Append(std::vector<unsigned char>& bytes,std::string_view value) {
  bytes.insert(bytes.end(),value.begin(),value.end());
}
void Append(std::vector<unsigned char>& bytes,uint32_t value,unsigned count=4) {
  for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<unsigned char>(value>>(8*n)));
}
std::vector<unsigned char> WaveHeader(const ArrangeCue& cue) {
  const auto pcm_bytes=cue.samples*4;
  std::vector<unsigned char> bytes;
  bytes.reserve(44);
  Append(bytes,"RIFF");Append(bytes,36+pcm_bytes+(cue.loop_length?68:0));
  Append(bytes,"WAVEfmt ");Append(bytes,16);Append(bytes,1,2);Append(bytes,2,2);
  Append(bytes,48000);Append(bytes,192000);Append(bytes,4,2);Append(bytes,16,2);
  Append(bytes,"data");Append(bytes,pcm_bytes);
  return bytes;
}
std::vector<unsigned char> WaveLoop(const ArrangeCue& cue) {
  if(!cue.loop_length)return {};
  std::vector<unsigned char> bytes;
  bytes.reserve(68);
  Append(bytes,"smpl");Append(bytes,60);
  for(const uint32_t field:{0u,0u,20833u,60u,0u,0u,0u,1u,0u,
                            0u,0u,cue.loop_start,cue.loop_start+cue.loop_length-1,0u,0u})
    Append(bytes,field);
  return bytes;
}
void CheckNoLink(const fs::path& path) {
  const auto flags=GetFileAttributesW(path.c_str());
  Require(flags==INVALID_FILE_ATTRIBUTES || !(flags&FILE_ATTRIBUTE_REPARSE_POINT),
          "X Arrange music directory contains a linked path");
}
std::array<Extent,kCues.size()> ReadWaveBank(WaveBank& disc,Extent bank) {
  std::array<unsigned char,52> header{};
  Require(bank.bytes>=header.size(),"Invalid X Arrange wave bank");
  disc.Read(bank.offset,header);
  Require(Magic(header,"DNBW") && ReadNumber(header,4,4,true)==45 && ReadNumber(header,8,4,true)==43,
          "Unsupported X Arrange wave bank format");
  const auto info=ReadNumber(header,12,4,true),info_bytes=ReadNumber(header,16,4,true);
  const auto table=ReadNumber(header,20,4,true),table_bytes=ReadNumber(header,24,4,true);
  const auto wave=ReadNumber(header,44,4,true),wave_bytes=ReadNumber(header,48,4,true);
  const auto within=[&](uint32_t at,uint32_t size){return at<=bank.bytes && size<=bank.bytes-at;};
  Require(info_bytes>=8 && table_bytes>=24*kCues.size() && within(info,info_bytes) &&
          within(table,table_bytes) && within(wave,wave_bytes),"Invalid X Arrange wave bank sections");
  std::array<unsigned char,8> info_prefix{};
  disc.Read(bank.offset+info,info_prefix);
  Require(ReadNumber(info_prefix,0,4,true)==0x80001 && uint64_t(ReadNumber(info_prefix,4,4,true))*24==table_bytes,
          "Invalid X Arrange wave bank table");
  std::array<Extent,kCues.size()> result{};
  for(size_t index=0;index<kCues.size();++index) {
    const auto& cue=kCues[index];
    std::array<unsigned char,24> entry{};
    disc.Read(bank.offset+table+24*index,entry);
    const auto frames=ReadNumber(entry,0,4,true)>>4,format=ReadNumber(entry,4,4,true);
    const auto at=ReadNumber(entry,8,4,true),size=ReadNumber(entry,12,4,true);
    Require(format==0x82177008 && frames==cue.samples && size==4*frames &&
            at<=wave_bytes && size<=wave_bytes-at && ReadNumber(entry,16,4,true)==cue.loop_start &&
            ReadNumber(entry,20,4,true)==cue.loop_length,"Unexpected X Arrange PCM track");
    result[index]={bank.offset+wave+at,size};
  }
  return result;
}
}
std::span<const ArrangeCue> ArrangeCues(){return kCues;}
std::span<const ArrangeBank> ArrangeBanks(){return kBanks;}
ArrangeStatus InspectArrangeMusic(const fs::path& data,CheckLevel level,VerificationCache* cache) {
  ArrangeStatus result;
  try {
    const auto directory=DataChild(data,"x-arrange");
    if(!fs::exists(directory))return result;
    CheckNoLink(directory);
    Require(fs::is_directory(directory),"X Arrange path must be a directory");
    bool present=false;
    for(const auto& bank:kBanks)present|=fs::exists(directory/bank.name);
    if(!present)return result; // A pre-created folder with only README is optional.
    for(const auto& bank:kBanks) {
      result.file=std::string(bank.name);
      const auto path=DataChild(directory,bank.name);
      if(!fs::exists(path)) {result.state=ArrangeState::Missing;return result;}
      CheckNoLink(path);
      if(!fs::is_regular_file(path) || fs::file_size(path)!=bank.bytes) {
        result.state=ArrangeState::Invalid;return result;
      }
      if(level!=CheckLevel::Quick) {
        BankLock locked(path);
        if(FileDigest(path,cache)!=bank.sha256) {result.state=ArrangeState::Invalid;return result;}
      }
    }
    result.state=ArrangeState::Ready;result.file.clear();
  } catch(const std::exception& error) {result.state=ArrangeState::Invalid;result.detail=error.what();}
  return result;
}
void ReadArrangeWaves(const fs::path& data,
    const std::function<void(std::string_view,std::span<const uint8_t>)>& consume,VerificationCache* cache) {
  const auto directory=DataChild(data,"x-arrange");
  CheckNoLink(directory);
  const auto sound=DataChild(directory,kBanks[0].name),wave=DataChild(directory,kBanks[1].name);
  CheckNoLink(sound);CheckNoLink(wave);
  BankLock sound_lock(sound),wave_lock(wave);
  const auto inspected=InspectArrangeMusic(data,CheckLevel::Full,cache);
  Require(inspected.state==ArrangeState::Ready,"X Arrange music banks changed during preparation");
  WaveBank input(wave);
  const auto tracks=ReadWaveBank(input,{0,kBanks[1].bytes});
  for(size_t index=0;index<kCues.size();++index) {
    const auto& cue=kCues[index];
    auto bytes=WaveHeader(cue);
    const auto header_size=bytes.size();
    bytes.resize(header_size+static_cast<size_t>(tracks[index].bytes));
    auto pcm=std::span(bytes).subspan(header_size);
    input.Read(tracks[index].offset,pcm);
    for(size_t byte=0;byte<pcm.size();byte+=2)std::swap(pcm[byte],pcm[byte+1]);
    const auto loop=WaveLoop(cue);
    bytes.insert(bytes.end(),loop.begin(),loop.end());
    consume(cue.name,bytes);
  }
}
}
