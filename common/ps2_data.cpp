#include "common/ps2_data.h"
#include "common/verification_cache.h"
#include "common/paths.h"
#include "backends/ps2/iso_disc.h"
#include "backends/ps2/disc_image.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace cave {
namespace {
std::string DiscDigest(const fs::path& image,ps2::DiscImage& stream,uint64_t offset,uint64_t bytes,VerificationCache* cache) {
  if(!ps2::DiscImage::IsChd(image))return FileDigestRange(image,offset,bytes,cache);
  return cache?cache->DigestRange(image,offset,bytes,true):Sha256Range(stream,stream.Size(),offset,bytes);
}
}
void InspectPs2Disc(const Game& game,const fs::path& directory,CheckLevel level,Inspection& result,VerificationCache* cache) {
  const DataFile *boot=nullptr,*disc=nullptr;
  for(const auto& file:game.files) {
    if(file.program && !boot)boot=&file;
    else if(!file.program && file.path=="disc.iso" && !disc)disc=&file;
    else throw std::runtime_error("Invalid PS2 disc manifest");
  }
  if(!boot || !disc)throw std::runtime_error("PS2 manifest needs a boot entry and disc identity");
  result.layout="iso";
  if(!fs::exists(directory)) {result.data_state=DataState::Missing;return;}
  std::vector<fs::path> candidates;
  auto add=[&](const fs::path& image) {
    if(_wcsicmp(image.extension().c_str(),L".iso") && !ps2::DiscImage::IsChd(image))return;
    const auto attributes=GetFileAttributesW(image.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))
      throw std::runtime_error("The game disc image must be a regular file");
    candidates.push_back(DataChild(image.parent_path(),Utf8(image.filename())));
    if(candidates.size()>64)throw std::runtime_error("Select a single ISO or CHD, or a directory with fewer disc images");
  };
  if(fs::is_directory(directory)) {
    unsigned examined=0;
    for(const auto& item:fs::directory_iterator(directory)) {
      if(++examined>4096)throw std::runtime_error("Select a single disc image or a smaller directory");
      add(item.path());
    }
  } else add(directory);
  std::sort(candidates.begin(),candidates.end());
  std::vector<fs::path> matches;
  std::vector<std::string> rejected;
  for(const auto& image:candidates) {
    ++result.files_checked;
    try {
      const auto size=fs::file_size(image);
      const auto modified=fs::last_write_time(image);
      ps2::DiscImage stream(image);
      const auto entry=ps2::FindIsoFile(stream,stream.Size(),boot->path);
      if(!entry || entry->size!=boot->bytes) {
        rejected.push_back("different boot program: "+Utf8(image.filename()));continue;
      }
      ++result.hashes_checked;
      if(DiscDigest(image,stream,uint64_t(entry->lba)*2048,entry->size,cache)!=boot->sha256 ||
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
    result.reason=candidates.empty()?"Select the original game ISO or CHD":"No supported game disc image found";
    result.problems=std::move(rejected);return;
  }
  if(matches.size()!=1) {
    result.data_state=DataState::Unknown;
    result.reason="More than one supported disc image found; select one file";
    for(const auto& file:matches)result.problems.push_back("duplicate game disc image: "+Utf8(file.filename()));
    return;
  }
  result.disc_image=matches.front();
  const auto size=fs::file_size(result.disc_image);
  const auto modified=fs::last_write_time(result.disc_image);
  ps2::DiscImage stream(result.disc_image);
  result.layout=ps2::DiscImage::IsChd(result.disc_image)?"chd":"iso";
  if(stream.Size()!=disc->bytes) {
    result.data_state=DataState::Incomplete;
    result.reason="The game disc size differs from the supported disc";return;
  }
  if(level==CheckLevel::Full) {
    ++result.hashes_checked;
    if(DiscDigest(result.disc_image,stream,0,stream.Size(),cache)!=disc->sha256 || fs::file_size(result.disc_image)!=size ||
       fs::last_write_time(result.disc_image)!=modified) {
      result.data_state=DataState::Incomplete;
      result.reason="The game disc checksum differs from the supported disc";return;
    }
    result.data_state=DataState::Verified;
  } else result.data_state=DataState::Unverified;
}
}
