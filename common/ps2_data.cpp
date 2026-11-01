#include "common/ps2_data.h"
#include "common/paths.h"
#include "backends/ps2/iso_disc.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace cave {
void InspectPs2Disc(const Game& game,const fs::path& directory,CheckLevel level,Inspection& result) {
  const DataFile *boot=nullptr,*disc=nullptr;
  for(const auto& file:game.files) {
    if(file.program && !boot)boot=&file;
    else if(!file.program && file.path=="disc.iso" && !disc)disc=&file;
    else throw std::runtime_error("Invalid PS2 disc manifest");
  }
  if(!boot || !disc)throw std::runtime_error("PS2 manifest needs a boot entry and disc identity");
  result.layout="iso";
  if(!fs::exists(directory)) {result.data_state=DataState::Missing;return;}
  if(!fs::is_directory(directory))throw std::runtime_error("Select the directory containing the original ISO");
  std::vector<fs::path> candidates;
  unsigned examined=0;
  for(const auto& item:fs::directory_iterator(directory)) {
    if(++examined>4096)throw std::runtime_error("Select a smaller directory containing the game ISO");
    auto extension=item.path().extension().native();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c) {
      return c>=L'A' && c<=L'Z'?wchar_t(c-L'A'+L'a'):c;
    });
    if(extension!=L".iso")continue;
    const auto attributes=GetFileAttributesW(item.path().c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))
      throw std::runtime_error("The game ISO must be a regular file");
    candidates.push_back(DataChild(directory,Utf8(item.path().filename())));
    if(candidates.size()>64)throw std::runtime_error("Select a directory with fewer ISO images");
  }
  std::sort(candidates.begin(),candidates.end());
  std::vector<fs::path> matches;
  std::vector<std::string> rejected;
  for(const auto& image:candidates) {
    ++result.files_checked;
    try {
      const auto size=fs::file_size(image);
      const auto modified=fs::last_write_time(image);
      std::ifstream stream(image,std::ios::binary);
      const auto entry=ps2::FindIsoFile(stream,size,boot->path);
      if(!entry || entry->size!=boot->bytes) {
        rejected.push_back("different boot program: "+Utf8(image.filename()));continue;
      }
      ++result.hashes_checked;
      if(Sha256Range(image,uint64_t(entry->lba)*2048,entry->size)!=boot->sha256 ||
         fs::file_size(image)!=size || fs::last_write_time(image)!=modified) {
        rejected.push_back("unrecognized boot program: "+Utf8(image.filename()));continue;
      }
      matches.push_back(image);
    } catch(const std::exception& error) {
      rejected.push_back(Utf8(image.filename())+": "+error.what());
    }
  }
  if(matches.empty()) {
    result.data_state=candidates.empty()?DataState::Missing:DataState::Unsupported;
    result.reason=candidates.empty()?"Place the original game ISO in this directory":"No supported game ISO found";
    result.problems=std::move(rejected);return;
  }
  if(matches.size()!=1) {
    result.data_state=DataState::Unknown;
    result.reason="More than one supported ISO found; keep one copy in the selected directory";
    for(const auto& file:matches)result.problems.push_back("duplicate game ISO: "+Utf8(file.filename()));
    return;
  }
  result.disc_image=matches.front();
  const auto size=fs::file_size(result.disc_image);
  const auto modified=fs::last_write_time(result.disc_image);
  if(size!=disc->bytes) {
    result.data_state=DataState::Incomplete;
    result.reason="The game ISO size differs from the supported disc";return;
  }
  if(level==CheckLevel::Full) {
    ++result.hashes_checked;
    if(Sha256(result.disc_image)!=disc->sha256 || fs::file_size(result.disc_image)!=size ||
       fs::last_write_time(result.disc_image)!=modified) {
      result.data_state=DataState::Incomplete;
      result.reason="The game ISO checksum differs from the supported disc";return;
    }
    result.data_state=DataState::Verified;
  } else result.data_state=DataState::Unverified;
}
}
