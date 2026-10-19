#include "common/config_update.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <stdexcept>

namespace cave {
namespace {
std::string Lower(std::string value) {
  for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
std::string_view Trim(std::string_view value) {
  const auto first=value.find_first_not_of(" \t\r");
  return first==value.npos?std::string_view{}:value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
}
std::string EditIni(std::string_view before, std::span<const IniEdit> changes) {
  std::string after(before);
  for (const auto& change : changes) {
    if (change.section.empty() || change.key.empty() ||
        change.section.find_first_of("[]\r\n=")!=std::string::npos ||
        change.key.find_first_of("[]=\r\n")!=std::string::npos ||
        Trim(change.section)!=change.section || Trim(change.key)!=change.key ||
        change.value.find_first_of("\r\n\0",0,3)!=std::string::npos)
      throw std::runtime_error("Invalid configuration edit");
    const auto ini=Ini::Parse(after);
    const auto newline=after.find("\r\n")!=after.npos?"\r\n":"\n";
    if (const auto old=ini.Get(change.section,change.key)) {
      size_t start=0;
      for (size_t number=1;number<old->line;++number) start=after.find('\n',start)+1;
      auto end=after.find('\n',start); if(end==after.npos) end=after.size();
      if(end>start && after[end-1]=='\r') --end;
      start=after.find('=',start)+1;
      while(start<end && (after[start]==' ' || after[start]=='\t')) ++start;
      while(end>start && (after[end-1]==' ' || after[end-1]=='\t')) --end;
      after.replace(start,end-start,change.value);
    } else {
      size_t start=0,insert=std::string::npos;
      while(start<after.size()) {
        auto end=after.find('\n',start);if(end==after.npos)end=after.size();
        auto line=Trim(std::string_view(after).substr(start,end-start));
        if(start==0 && line.starts_with("\xef\xbb\xbf")){line.remove_prefix(3);line=Trim(line);}
        if(line.size()>=2 && line.front()=='[' && line.back()==']' &&
            Lower(std::string(Trim(line.substr(1,line.size()-2))))==Lower(change.section))
          {insert=end<after.size()?end+1:end;break;}
        start=end<after.size()?end+1:end;
      }
      const auto row=change.key+" = "+change.value+newline;
      if(insert!=after.npos)after.insert(insert,(insert && after[insert-1]!='\n'?std::string(newline):"")+row);
      else {if(!after.empty() && !after.ends_with('\n'))after+=newline;after+=std::string(newline)+"["+change.section+"]"+newline+row;}
    }
  }
  (void)Ini::Parse(after);
  return after;
}
void UpdateConfig(const fs::path& root, std::string_view before, std::span<const IniEdit> changes) {
  const auto config=EnsureConfig(root);
  const auto after=EditIni(before,changes);
  if(Ini::ReadText(config)!=before)
    throw std::runtime_error("config.ini has changed. Refresh the launcher before saving again.");
  if(after==before)return;
  std::array<unsigned char,16> nonce{};
  if(BCryptGenRandom(nullptr,nonce.data(),ULONG(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
    throw std::runtime_error("Cannot create configuration transaction");
  std::wstring suffix;
  for(auto byte:nonce)suffix+=std::to_wstring(byte)+L"-";
  const auto temporary=fs::path(config.native()+L".pending-"+suffix);
  HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create configuration transaction");
  struct Pending {
    fs::path path;
    ~Pending(){if(!path.empty())DeleteFileW(path.c_str());}
  } pending{temporary};
  DWORD written{};
  const bool okay=WriteFile(file,after.data(),DWORD(after.size()),&written,nullptr) && written==after.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if(!okay || Ini::ReadText(config)!=before) {
    throw std::runtime_error("Cannot save config.ini, or it changed while saving. Refresh and retry.");
  }
  if(!MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Cannot replace config.ini");
  }
  pending.path.clear();
}
}  // namespace cave
