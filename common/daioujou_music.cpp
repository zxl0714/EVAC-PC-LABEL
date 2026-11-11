#include "common/daioujou_music.h"
#include "common/ini.h"
#include "common/launch.h"
#include "common/settings.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <set>
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
constexpr std::string_view kSoundHash="6513daa1a4db3aacfa4b32cba6d862d40f5365bf507eee47c6a2413176ada5fc";
constexpr std::string_view kWaveHash="c89d558adb9fced6b868ddcb19e73d554feb98988f129ae17915de8fa434b6f0";
constexpr uint64_t kSector=2048;
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
bool AsciiEqual(std::span<const unsigned char> data,std::string_view value) {
  const auto lower=[](int c){return c>='A' && c<='Z'?c+('a'-'A'):c;};
  return data.size()==value.size() && std::ranges::equal(data,value,[&](auto a,auto b){return lower(a)==lower(b);});
}
class Disc {
 public:
  explicit Disc(const fs::path& path):size_(fs::file_size(path)),input_(path,std::ios::binary) {
    Require(bool(input_),"Cannot open the Xbox 360 ISO");
    for(const uint64_t partition:{0ull,0xfd90000ull,0x2080000ull,0x18300000ull}) {
      const auto volume_at=partition+32*kSector;
      if(volume_at+kSector>size_)continue;
      std::array<unsigned char,kSector> volume{};
      Read(volume_at,volume);
      constexpr std::string_view signature="MICROSOFT*XBOX*MEDIA";
      if(!Magic(volume,signature) || !Magic(std::span(volume).last(signature.size()),signature))continue;
      partition_=partition;
      root_=AtSector(ReadNumber(volume,20,4),ReadNumber(volume,24,4));
      return;
    }
    throw std::runtime_error("Select the Xbox 360 Black Label Extra game ISO.");
  }
  std::array<Extent,2> Banks() {
    const auto directory=Find(Find(root_,"dat",true),"sound",true);
    return {Find(directory,"ddo_bgm_SoundBank.xsb",false),Find(directory,"ddo_bgm_WaveBank.xwb",false)};
  }
  void Read(uint64_t at,std::span<unsigned char> bytes) {
    Require(at<=size_ && bytes.size()<=size_-at,"Xbox 360 ISO extent exceeds the file");
    input_.seekg(static_cast<std::streamoff>(at));
    if(!input_.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error("Cannot read the Xbox 360 ISO");
  }
 private:
  Extent AtSector(uint32_t sector,uint32_t size) const {
    const auto start=partition_+kSector*sector;
    Require(start<=size_ && size<=size_-start,"Invalid Xbox 360 disc directory extent");
    return {start,size};
  }
  Extent Find(Extent directory,std::string_view name,bool folder) {
    Require(directory.bytes>=14 && directory.bytes<=4*1024*1024,"Invalid Xbox 360 disc directory");
    std::vector<unsigned char> table(static_cast<size_t>(directory.bytes));
    Read(directory.offset,table);
    std::vector<size_t> pending{0};
    std::set<size_t> visited;
    std::optional<Extent> result;
    while(!pending.empty()) {
      const auto at=pending.back();
      pending.pop_back();
      Require(at<=table.size()-14 && visited.insert(at).second,"Invalid Xbox 360 disc directory tree");
      const auto name_bytes=table[at+13];
      Require(name_bytes && name_bytes<=table.size()-at-14,"Invalid Xbox 360 disc filename");
      if(AsciiEqual(std::span(table).subspan(at+14,name_bytes),name)) {
        Require(!result && bool(table[at+12]&0x10)==folder,"Ambiguous Xbox 360 disc path");
        result=AtSector(ReadNumber(table,at+4,4),ReadNumber(table,at+8,4));
      }
      for(const auto link:{ReadNumber(table,at,2),ReadNumber(table,at+2,2)})
        if(link)pending.push_back(size_t(link)*4);
    }
    Require(result.has_value(),"The disc does not contain the Black Label Extra music banks");
    return *result;
  }
  uint64_t size_,partition_{};
  std::ifstream input_;
  Extent root_;
};

std::string WaveName(const ArrangeCue& cue){return std::string(cue.name)+".wav";}
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
bool HasWave(const fs::path& path,const ArrangeCue& cue) {
  CheckNoLink(path);
  const auto header=WaveHeader(cue),loop=WaveLoop(cue);
  if(!fs::is_regular_file(path) || fs::file_size(path)!=header.size()+4ull*cue.samples+loop.size())return false;
  std::ifstream input(path,std::ios::binary);
  std::vector<unsigned char> buffer(header.size());
  if(!input.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size())) || buffer!=header)return false;
  if(loop.empty())return true;
  input.seekg(-static_cast<std::streamoff>(loop.size()),std::ios::end);
  buffer.resize(loop.size());
  return bool(input.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size()))) && buffer==loop;
}
std::array<Extent,kCues.size()> ReadWaveBank(Disc& disc,Extent bank) {
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
struct Staging {
  fs::path path;
  ~Staging() {
    for(const auto& cue:kCues)DeleteFileW((path/WaveName(cue)).c_str());
    RemoveDirectoryW(path.c_str());
  }
};
}
std::span<const ArrangeCue> ArrangeCues(){return kCues;}
fs::path ArrangeMusicDirectory(const fs::path& collection) {
  auto path=fs::canonical(collection);
  for(const auto* part:{"user-data","daioujou","x-arrange"}) {
    path/=part;
    CheckNoLink(path);
  }
  return path;
}
bool HasArrangeMusic(const fs::path& collection) {
  try {
    const auto directory=ArrangeMusicDirectory(collection);
    return std::ranges::all_of(kCues,[&](const auto& cue){return HasWave(directory/WaveName(cue),cue);});
  } catch(const std::exception&) {return false;}
}
void ImportArrangeMusic(const fs::path& collection,const fs::path& requested_iso,
                        const std::function<void(uint64_t,uint64_t)>& progress,
                        const std::function<bool()>& cancelled) {
  const auto root=fs::canonical(collection),iso=fs::canonical(requested_iso),directory=ArrangeMusicDirectory(root);
  Require(fs::is_regular_file(iso) && !Within(iso,directory),"Select an Xbox 360 game ISO outside the music folder");
  const auto ini=Ini::Load(ConfigPath(root));
  const auto overlaps=[&](const fs::path& path){return Within(directory,path) || Within(path,directory);};
  for(const auto& game:Catalogue()) {
    const auto data=Resolve(root,ini.DataDirectory(game));
    const auto dlc=ini.ContentDirectory(game,data);
    Require(!overlaps(data) && (dlc.empty() || !overlaps(Resolve(root,dlc))),
            "X Arrange music folder overlaps configured game data");
  }
  GameSession session(root);
  const auto check=[&]{if(cancelled && cancelled())throw std::runtime_error("X Arrange music import cancelled");};
  check();
  // Keep the verified source stable through hashing and PCM export.
  const auto locked=CreateFileW(iso.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Require(locked!=INVALID_HANDLE_VALUE,"Cannot lock the Xbox 360 ISO for reading");
  struct Unlock {HANDLE handle;~Unlock(){CloseHandle(handle);}} unlock{locked};
  Disc disc(iso);
  const auto banks=disc.Banks();
  Require(banks[0].bytes==2737 && banks[1].bytes==736970752 &&
          Sha256Range(iso,banks[0].offset,banks[0].bytes)==kSoundHash &&
          Sha256Range(iso,banks[1].offset,banks[1].bytes)==kWaveHash,
          "Select the supported Japanese Black Label Extra Xbox 360 ISO.");
  const auto tracks=ReadWaveBank(disc,banks[1]);
  uint64_t total=0,done=0;
  for(const auto& track:tracks)total+=track.bytes;
  if(progress)progress(done,total);
  check();
  fs::create_directories(directory);
  const auto stage=directory/("import-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  Require(fs::create_directory(stage),"Cannot create the X Arrange music staging folder");
  Staging cleanup{stage};
  std::vector<unsigned char> buffer(512*1024);
  for(size_t index=0;index<kCues.size();++index) {
    const auto& cue=kCues[index];
    const auto path=stage/WaveName(cue);
    std::ofstream output(path,std::ios::binary);
    const auto write=[&](std::span<const unsigned char> bytes) {
      if(bytes.empty())return;
      output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
      Require(bool(output),"Cannot write the X Arrange music files");
    };
    write(WaveHeader(cue));
    for(uint64_t offset=0;offset<tracks[index].bytes;) {
      check();
      const auto count=static_cast<size_t>((std::min)(uint64_t(buffer.size()),tracks[index].bytes-offset));
      const auto chunk=std::span(buffer).first(count);
      disc.Read(tracks[index].offset+offset,chunk);
      for(size_t byte=0;byte<count;byte+=2)std::swap(chunk[byte],chunk[byte+1]);
      write(chunk);
      offset+=count;done+=count;
    }
    if(progress)progress(done,total);
    write(WaveLoop(cue));
    output.close();
    Require(bool(output) && HasWave(path,cue),"Incomplete X Arrange music file");
  }
  check();
  for(const auto& cue:kCues) {
    const auto name=WaveName(cue);
    const auto target=directory/name;
    CheckNoLink(target);
    Require(MoveFileExW((stage/name).c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,
            "Cannot install the imported X Arrange music");
  }
  Require(HasArrangeMusic(root),"Installed X Arrange music is incomplete");
}
}
