#include "iso_disc.h"
#include <algorithm>
#include <array>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cave::ps2 {
namespace {
constexpr uint64_t Sector=2048;
using Bytes=std::vector<uint8_t>;
uint32_t Both(const uint8_t* data,unsigned count) {
  uint32_t little{},big{};
  for(unsigned i=0;i<count;++i) {
    little|=uint32_t(data[i])<<(8*i);
    big=(big<<8)|data[count+i];
  }
  if(little!=big)throw std::runtime_error("ISO endian fields disagree");
  return little;
}
Bytes Read(std::istream& stream,uint64_t limit,uint64_t offset,size_t count) {
  if(offset>limit || count>limit-offset)throw std::runtime_error("ISO extent exceeds image");
  Bytes result(count);stream.clear();stream.seekg(static_cast<std::streamoff>(offset));
  stream.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(count));
  if(!stream)throw std::runtime_error("ISO read failed");
  return result;
}
std::string Name(std::string value) {
  if(value.empty() || value=="." || value=="..")throw std::runtime_error("Invalid ISO path component");
  if(const auto version=value.find(';');version!=std::string::npos) {
    if(version+1==value.size() || !std::all_of(value.begin()+version+1,value.end(),
        [](unsigned char c){return c>='0' && c<='9';}))throw std::runtime_error("Invalid ISO file version");
    value.resize(version);
  }
  while(!value.empty() && value.back()=='.')value.pop_back();
  if(value.empty())throw std::runtime_error("Invalid ISO filename");
  for(char& character:value) {
    const auto c=static_cast<unsigned char>(character);
    if(c<32 || c>126 || c==':' || c=='/' || c=='\\')throw std::runtime_error("Invalid ISO filename");
    if(c>='a' && c<='z')character=char(c-'a'+'A');
  }
  return value;
}
std::vector<std::string> Components(std::string path) {
  for(char& c:path) {if(c=='\\')c='/';else if(c>='a' && c<='z')c=char(c-'a'+'A');}
  for(std::string_view prefix:{"CDROM0:","CDROM:"})
    if(path.starts_with(prefix)) {path.erase(0,prefix.size());break;}
  while(path.starts_with('/'))path.erase(0,1);
  std::vector<std::string> parts;
  size_t start=0;
  while(start<path.size()) {
    const auto slash=path.find('/',start);
    parts.push_back(Name(path.substr(start,slash==std::string::npos?slash:slash-start)));
    if(parts.size()>32)throw std::runtime_error("ISO path exceeds directory limit");
    if(slash==std::string::npos)break;
    start=slash+1;
    if(start==path.size())throw std::runtime_error("ISO path names a directory");
  }
  if(parts.empty())throw std::runtime_error("Missing ISO filename");
  return parts;
}
struct Record {IsoFile file;bool directory;};
Record Parse(const uint8_t* data,size_t available,uint64_t limit) {
  const auto length=data[0];
  if(available<34 || length<34 || length>available || 33u+data[32]>length ||
     data[1] || data[26] || data[27] || (data[25]&0x80))
    throw std::runtime_error("Unsupported ISO directory record");
  Record record{{Both(data+2,4),Both(data+10,4)},bool(data[25]&2)};
  if(uint64_t(record.file.lba)*Sector>limit || record.file.size>limit-uint64_t(record.file.lba)*Sector)
    throw std::runtime_error("ISO directory entry exceeds image");
  return record;
}
}

std::optional<IsoFile> FindIsoFile(std::istream& image,uint64_t imageSize,std::string_view path) {
  const auto parts=Components(std::string(path));
  Bytes primary;bool terminated=false;
  for(unsigned sector=16;sector<80;++sector) {
    auto descriptor=Read(image,imageSize,sector*Sector,Sector);
    if(!std::equal(descriptor.begin()+1,descriptor.begin()+7,"CD001\1"))
      throw std::runtime_error("Expected a sector-aligned ISO9660 image");
    if(descriptor[0]==1 && primary.empty())primary=std::move(descriptor);
    else if(descriptor[0]==255) {terminated=true;break;}
  }
  if(!terminated || primary.empty() || Both(primary.data()+128,2)!=Sector)
    throw std::runtime_error("Missing ISO9660 primary volume");
  const uint64_t limit=uint64_t(Both(primary.data()+80,4))*Sector;
  if(limit>imageSize || limit<18*Sector)throw std::runtime_error("Truncated ISO volume");
  auto directory=Parse(primary.data()+156,primary.size()-156,limit);
  for(size_t component=0;component<parts.size();++component) {
    if(!directory.directory || directory.file.size>16*1024*1024)
      throw std::runtime_error("Invalid ISO directory extent");
    const auto bytes=Read(image,limit,uint64_t(directory.file.lba)*Sector,directory.file.size);
    std::optional<Record> match;
    for(size_t offset=0;offset<bytes.size();) {
      const unsigned length=bytes[offset];
      if(!length) {offset=(offset/Sector+1)*Sector;continue;}
      if(length>Sector-offset%Sector)throw std::runtime_error("ISO record crosses a sector boundary");
      const auto record=Parse(bytes.data()+offset,bytes.size()-offset,limit);
      const auto nameLength=bytes[offset+32];
      const auto* name=bytes.data()+offset+33;
      if(!(nameLength==1 && name[0]<=1) &&
         Name(std::string(reinterpret_cast<const char*>(name),nameLength))==parts[component]) {
        if(match)throw std::runtime_error("Ambiguous ISO directory entry");
        match=record;
      }
      offset+=length;
    }
    if(!match)return std::nullopt;
    directory=*match;
  }
  return directory.directory?std::nullopt:std::optional<IsoFile>(directory.file);
}
}
