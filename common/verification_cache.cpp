#include "common/verification_cache.h"
#include "common/paths.h"
#include "common/ini.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cave {
namespace {
constexpr std::string_view Header="EVAC file verification 1\n";
constexpr size_t MaxEntries=8192, MaxBytes=MaxEntries*130+128;
struct Handle {
  HANDLE value{INVALID_HANDLE_VALUE};
  ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
std::string HashRange(HANDLE file,uint64_t offset,uint64_t bytes) {
  struct Hash {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE value{};
    ~Hash(){if(value)BCryptDestroyHash(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
  } hash;
  if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
     BCryptCreateHash(hash.algorithm,&hash.value,nullptr,0,nullptr,0,0)<0)
    throw std::runtime_error("Cannot initialize file verification");
  LARGE_INTEGER size{},position{};position.QuadPart=offset;
  if(!GetFileSizeEx(file,&size) || offset>uint64_t(size.QuadPart) || bytes>uint64_t(size.QuadPart)-offset ||
     !SetFilePointerEx(file,position,nullptr,FILE_BEGIN))throw std::runtime_error("Invalid verification range");
  std::array<unsigned char,256*1024> block;
  while(bytes) {
    const auto count=DWORD(std::min<uint64_t>(bytes,block.size()));DWORD received{};
    if(!ReadFile(file,block.data(),count,&received,nullptr) || received!=count ||
       BCryptHashData(hash.value,block.data(),count,0)<0)throw std::runtime_error("File verification read failed");
    bytes-=count;
  }
  std::array<unsigned char,32> digest{};
  if(BCryptFinishHash(hash.value,digest.data(),ULONG(digest.size()),0)<0)
    throw std::runtime_error("File verification hash failed");
  constexpr char hex[]="0123456789abcdef";
  std::string result;
  for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}
  return result;
}
bool Hex(std::string_view value) {
  return value.size()==64 && value.find_first_not_of("0123456789abcdef")==value.npos;
}
std::string DigestText(std::string_view bytes) {
  BCRYPT_ALG_HANDLE algorithm{};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
    throw std::runtime_error("Cannot initialize verification identity");
  std::array<unsigned char,32> digest{};
  const auto status=BCryptHash(algorithm,nullptr,0,
      reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));
  BCryptCloseAlgorithmProvider(algorithm,0);
  if(status<0)throw std::runtime_error("Cannot hash verification identity");
  constexpr char hex[]="0123456789abcdef";
  std::string result;
  for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}
  return result;
}
std::map<std::string,std::string> Load(const fs::path& file) {
  std::map<std::string,std::string> result;
  std::error_code error;
  const auto size=fs::file_size(file,error);
  if(error || size>MaxBytes || size<Header.size()+65)return result;
  std::ifstream input(file,std::ios::binary);
  std::string bytes(size,'\0');
  if(!input.read(bytes.data(),std::streamsize(size)) || !bytes.starts_with(Header))return result;
  const auto body=std::string_view(bytes).substr(0,bytes.size()-65);
  if(bytes.back()!='\n' || DigestText(body)!=std::string_view(bytes).substr(bytes.size()-65,64))return result;
  std::istringstream lines(std::string(body.substr(Header.size())));
  std::string line;
  while(std::getline(lines,line)) {
    if(line.size()!=129 || line[64]!=' ' || !Hex(std::string_view(line).substr(0,64)) ||
       !Hex(std::string_view(line).substr(65)) || result.size()>=MaxEntries)return {};
    if(!result.emplace(line.substr(0,64),line.substr(65)).second)return {};
  }
  return result;
}
std::string Identity(HANDLE file,uint64_t offset,uint64_t bytes) {
  FILE_ID_INFO id{};
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO standard{};
  wchar_t filesystem[32]{};
  if(!GetFileInformationByHandleEx(file,FileIdInfo,&id,sizeof(id)) ||
     !GetFileInformationByHandleEx(file,FileBasicInfo,&basic,sizeof(basic)) ||
     !GetFileInformationByHandleEx(file,FileStandardInfo,&standard,sizeof(standard)) ||
     !GetVolumeInformationByHandleW(file,nullptr,0,nullptr,nullptr,nullptr,filesystem,32) ||
     (std::wstring_view(filesystem)!=L"NTFS" && std::wstring_view(filesystem)!=L"ReFS") ||
     basic.ChangeTime.QuadPart<=0)return {};
  std::string stamp;
  auto append=[&](const auto& value){stamp.append(reinterpret_cast<const char*>(&value),sizeof(value));};
  append(id.VolumeSerialNumber);append(id.FileId.Identifier);
  append(basic.CreationTime.QuadPart);append(basic.LastWriteTime.QuadPart);append(basic.ChangeTime.QuadPart);
  append(standard.EndOfFile.QuadPart);append(offset);append(bytes);
  return DigestText(stamp);
}
}
VerificationCache::VerificationCache(const fs::path& root) {
  try {
    directory_=DataChild(root,"user-data/cache");
    path_=DataChild(root,"user-data/cache/verification-v1.txt");
    const auto ini=Ini::Load(ConfigPath(root));
    for(const auto& game:Catalogue()) {
      const auto data=Resolve(root,ini.DataDirectory(game));
      if(Within(directory_,data) || Within(data,directory_))throw std::runtime_error("Verification cache overlaps game data");
    }
    entries_=Load(path_);
  } catch(...) {directory_.clear();path_.clear();}
}
VerificationCache::~VerificationCache(){Flush();}
std::string VerificationCache::Digest(const fs::path& file){return DigestRange(file,0,fs::file_size(file));}
std::string VerificationCache::DigestRange(const fs::path& file,uint64_t offset,uint64_t bytes) {
  // Keep the same file alive and deny writes/replacement while checking and hashing.
  Handle handle{CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
  if(handle.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file for verification (Windows "+std::to_string(GetLastError())+")");
  const auto identity=Identity(handle.value,offset,bytes);
  if(!identity.empty()) {
    const auto found=entries_.find(identity);
    if(found!=entries_.end()){++hits;return found->second;}
  }
  const auto digest=HashRange(handle.value,offset,bytes);
  ++reads;bytes_read+=bytes;
  if(!identity.empty() && identity==Identity(handle.value,offset,bytes)) {
    entries_[identity]=digest;
    pending_[identity]=digest;
  }
  return digest;
}
void VerificationCache::Flush() noexcept {
  if(pending_.empty() || path_.empty())return;
  fs::path temporary;
  try {
    fs::create_directories(directory_);
    // A competing verification may finish first. Merge under a short commit lock.
    // A busy or read-only cache simply loses this optimization for this call.
    Handle lock{CreateFileW((directory_/"verification.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,
                           nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr)};
    if(lock.value==INVALID_HANDLE_VALUE)return;
    auto merged=Load(path_);
    for(const auto& [key,value]:pending_)merged[key]=value;
    if(merged.size()>MaxEntries)merged=pending_;
    if(merged.size()>MaxEntries)return;
    std::string text(Header);
    for(const auto& [key,value]:merged)text+=key+' '+value+'\n';
    text+=DigestText(text)+'\n';
    temporary=directory_/("verification-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetCurrentThreadId())+".tmp");
    {
      Handle output{CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
      if(output.value==INVALID_HANDLE_VALUE)return;
      DWORD written{};
      if(!WriteFile(output.value,text.data(),DWORD(text.size()),&written,nullptr) || written!=text.size())
        throw std::runtime_error("Cannot save verification cache");
    }
    if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Cannot replace verification cache");
    pending_.clear();
  } catch(...) {if(!temporary.empty())DeleteFileW(temporary.c_str());}
}
std::string FileDigest(const fs::path& file,VerificationCache* cache){return cache?cache->Digest(file):Sha256(file);}
std::string FileDigestRange(const fs::path& file,uint64_t offset,uint64_t bytes,VerificationCache* cache) {
  return cache?cache->DigestRange(file,offset,bytes):Sha256Range(file,offset,bytes);
}
}
