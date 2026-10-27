#include "common/stfs.h"
#include "common/paths.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cave {
namespace {
constexpr uint32_t kBlock=4096, kEnd=0xFFFFFF, kMax=512*1024*1024;
void Need(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Digest {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  explicit Digest(const wchar_t* name) {
    Need(BCryptOpenAlgorithmProvider(&algorithm,name,nullptr,0)>=0,"Cannot initialize package hash");
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,BCRYPT_HASH_REUSABLE_FLAG)<0) {
      BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
      throw std::runtime_error("Cannot initialize reusable package hash");
    }
  }
  ~Digest(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
  void Add(const uint8_t* data,uint32_t size) {
    Need(BCryptHashData(hash,const_cast<PUCHAR>(data),size,0)>=0,"Package hash failed");
  }
  template<size_t N> std::array<uint8_t,N> Finish() {
    std::array<uint8_t,N> value{};
    Need(BCryptFinishHash(hash,value.data(),ULONG(N),0)>=0,"Package hash finish failed");return value;
  }
};
void NoLinks(fs::path path) {
  for(;!path.empty();) {
    const auto attr=GetFileAttributesW(path.c_str());
    Need(attr==INVALID_FILE_ATTRIBUTES || !(attr&FILE_ATTRIBUTE_REPARSE_POINT),"Linked package/cache path");
    const auto parent=path.parent_path();if(parent==path)break;path=parent;
  }
}
std::string Lower(std::string text) {
  for(auto& c:text)if(c>='A' && c<='Z')c=char(c-'A'+'a');return text;
}
void SafeName(const std::string& name) {
  Need(!name.empty() && name!="." && name!=".." && name.back()!='.' && name.back()!=' ',"Invalid package filename");
  for(unsigned char c:name)Need(c>=32 && c<=126 && std::string_view("<>:\"/\\|?*").find(char(c))==std::string_view::npos,"Unsafe package filename");
  auto stem=Lower(name.substr(0,name.find('.')));while(!stem.empty() && stem.back()==' ')stem.pop_back();
  Need(stem!="con" && stem!="prn" && stem!="aux" && stem!="nul" &&
       !(stem.size()==4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3]>='0' && stem[3]<='9'),"Reserved package filename");
}
}
struct StfsPackage::Impl {
  HANDLE file{INVALID_HANDLE_VALUE},mapping{};
  const uint8_t* data{};
  uint32_t size{},first{},blocks{},title{};
  bool verified{};
  std::vector<StfsFile> files;
  ~Impl(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
  uint32_t Number(uint32_t at,uint32_t count,bool little=false) const {
    Need(at<=size && count<=size-at,"Truncated package");uint32_t result=0;
    for(uint32_t i=0;i<count;++i)result=(result<<8)|data[at+(little?count-i-1:i)];return result;
  }
  uint32_t HashOffset(uint32_t block) const {
    Need(block<blocks,"Package block outside allocation");
    const auto table=block<170?0:(block/170)*171+block/28900+1+(block>=28900?1:0);
    const auto offset=first+table*kBlock+(block%170)*24;
    Need(offset<=size && 24<=size-offset,"Truncated package hash table");return offset;
  }
  uint32_t Offset(uint32_t block) const {
    (void)HashOffset(block);auto physical=block;
    for(uint32_t span=170,level=0;level<3;++level,span*=170) {physical+=(block+span)/span;if(block<span)break;}
    const auto offset=first+physical*kBlock;
    Need(offset<=size && kBlock<=size-offset,"Truncated package data block");return offset;
  }
  void Load(const fs::path& source,bool verify,uint32_t content_type) {
    Need(content_type==2 || content_type==0xD0000,"Unsupported content type");
    NoLinks(source);
    file=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(file!=INVALID_HANDLE_VALUE,"Cannot read content package");
    LARGE_INTEGER length{};Need(GetFileSizeEx(file,&length) && length.QuadPart>=0x400 && length.QuadPart<=kMax,"Expected a content package up to 512 MiB");
    size=uint32_t(length.QuadPart);
    mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);Need(mapping!=nullptr,"Cannot map DLC container");
    data=static_cast<const uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));Need(data!=nullptr,"Cannot view DLC container");
    Need(!std::memcmp(data,"LIVE",4) || !std::memcmp(data,"PIRS",4),"Expected a LIVE/PIRS content package; unpack ZIP/RAR first");
    auto header=Number(0x340,4);Need(header>=0x400 && header<=1024*1024 && header<=size,"Invalid package header");
    first=(header+4095)&~4095u;
    Need(Number(0x344,4)==content_type,content_type==2?"Expected DLC content type":"Expected XBLA content type");title=Number(0x360,4);
    Need(Number(0x3A9,4)==0 && data[0x379]==0x24 && (data[0x37B]&1),"Unsupported STFS layout");
    blocks=Number(0x395,4);Need(blocks>0 && blocks<kMax/kBlock,"Package block count exceeds limit");(void)Offset(blocks-1);
    std::set<uint32_t> used;
    Digest sha1(BCRYPT_SHA1_ALGORITHM),sha256(BCRYPT_SHA256_ALGORITHM);
    const auto check=[&](uint32_t block) {
      Need(used.insert(block).second,"Reused package block or cyclic chain");const auto at=Offset(block);
      if(verify) {sha1.Add(data+at,kBlock);const auto value=sha1.Finish<20>();Need(!std::memcmp(value.data(),data+HashOffset(block),20),"DLC block checksum mismatch");}
      return at;
    };
    const auto next=[&](uint32_t block){return Number(HashOffset(block)+21,3);};
    const auto count=Number(0x37C,2,true);auto current=Number(0x37E,3,true);
    Need(count>0 && count<256,"Invalid package file table size");std::vector<uint32_t> tables;
    for(uint32_t n=0;n<count;++n){tables.push_back(check(current));current=next(current);}Need(current==kEnd,"File table chain mismatch");
    struct Slot {std::string path;bool directory{};};
    std::vector<Slot> slots(count*64);std::set<std::string> names;
    for(uint32_t n=0;n<count;++n)for(uint32_t slot=0;slot<64;++slot) {
      const auto at=tables[n]+slot*64,lengthName=uint32_t(data[at+40]&63);if(!lengthName)continue;
      Need(lengthName<=40,"Invalid package filename length");
      std::string name(reinterpret_cast<const char*>(data+at),lengthName);SafeName(name);
      auto relative=name;const auto parent=Number(at+50,2);
      if(parent!=65535){Need(parent<n*64+slot && slots[parent].directory,"Invalid package parent");relative=slots[parent].path+"/"+name;}
      Need(relative.size()<=200 && names.insert(Lower(relative)).second,"Duplicate or excessive package path");
      const bool directory=(data[at+40]&128)!=0;slots[n*64+slot]={relative,directory};if(directory)continue;
      StfsFile entry;entry.path=relative;entry.bytes=Number(at+52,4);Need(entry.bytes<=kMax,"Excessive package file size");
      const auto expected=(entry.bytes+4095)/4096;Need(Number(at+44,3,true)==expected,"File size/block count mismatch");
      current=Number(at+47,3,true);auto remaining=entry.bytes;
      for(uint32_t b=0;b<expected;++b){const auto offset=check(current);entry.offsets.push_back(offset);if(verify)sha256.Add(data+offset,std::min(remaining,kBlock));remaining-=std::min(remaining,kBlock);current=next(current);}
      Need(!expected || current==kEnd,"File chain length mismatch");
      if(verify){constexpr char hex[]="0123456789abcdef";for(auto byte:sha256.Finish<32>()){entry.sha256+=hex[byte>>4];entry.sha256+=hex[byte&15];}}
      files.push_back(std::move(entry));
    }
    Need(!files.empty(),"Empty DLC container");verified=verify;
  }
};
StfsPackage::StfsPackage(const fs::path& source,bool verify,uint32_t content_type):impl_(std::make_unique<Impl>()){impl_->Load(source,verify,content_type);}
StfsPackage::~StfsPackage()=default;
uint32_t StfsPackage::TitleId()const{return impl_->title;}
const std::vector<StfsFile>& StfsPackage::Files()const{return impl_->files;}
void StfsPackage::Extract(const fs::path& output)const {
  Need(impl_->verified,"DLC extraction requires verified data");NoLinks(output);
  Need(!fs::exists(output),"DLC cache destination already exists");fs::create_directories(output.parent_path());
  Need(fs::create_directory(output),"Cannot create DLC cache destination");
  for(const auto& entry:impl_->files) {
    const auto target=DataChild(output,entry.path);NoLinks(target);fs::create_directories(target.parent_path());
    HANDLE handle=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(handle!=INVALID_HANDLE_VALUE,"Cannot create DLC cache file");
    try {
      auto remaining=entry.bytes;
      for(const auto at:entry.offsets){const auto amount=std::min(remaining,kBlock);DWORD written{};Need(WriteFile(handle,impl_->data+at,amount,&written,nullptr) && written==amount,"Cannot write DLC cache file");remaining-=amount;}
    } catch(...){CloseHandle(handle);throw;}
    CloseHandle(handle);
  }
}
}
