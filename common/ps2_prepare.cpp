#include "common/ps2_prepare.h"
#include "backends/ps2/iso_disc.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cave {
fs::path PreparePs2Disc(const GamePaths& paths,const fs::path& image,std::string_view identity,
                        std::span<const Ps2PreparedFile> files) {
  ValidateOutputs(paths);
  if(identity.size()!=64 || identity.find_first_not_of("0123456789abcdef")!=identity.npos ||
     files.empty() || files.size()>128)throw std::runtime_error("Invalid prepared disc metadata");
  std::set<fs::path> names;
  uint64_t total{};
  for(const auto& file:files) {
    const auto name=DataChild(paths.prepared,file.path);
    if(!names.insert(name).second || file.bytes>32u*1024u*1024u ||
       total+file.bytes>64u*1024u*1024u)throw std::runtime_error("Invalid prepared disc file inventory");
    total+=file.bytes;
  }
  const auto destination=DataChild(paths.prepared,"disc-"+std::string(identity));
  auto valid=[&](const fs::path& root) {
    if(!fs::is_directory(root))return false;
    std::set<fs::path> expected;
    for(const auto& file:files) {
      const auto path=DataChild(root,file.path);
      if(!fs::is_regular_file(path) || fs::file_size(path)!=file.bytes || Sha256(path)!=file.sha256)return false;
      expected.insert(FromUtf8(file.path));
    }
    size_t count=0;
    for(const auto& entry:fs::recursive_directory_iterator(root)) {
      const auto attributes=GetFileAttributesW(entry.path().c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
      if(entry.is_directory())continue;
      if(!entry.is_regular_file() || !expected.contains(entry.path().lexically_relative(root)))return false;
      ++count;
    }
    return count==files.size();
  };
  if(valid(destination))return destination;
  fs::create_directories(paths.prepared);
  const auto stage=DataChild(paths.prepared,"prepare-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  if(!fs::create_directory(stage))throw std::runtime_error("Prepared disc transaction collision");
  std::ifstream source(image,std::ios::binary);
  const auto bytes=fs::file_size(image);
  const auto modified=fs::last_write_time(image);
  for(const auto& file:files) {
    const auto extent=ps2::FindIsoFile(source,bytes,file.disc_path);
    if(!extent || extent->size!=file.bytes)throw std::runtime_error("Disc is missing a required boot/IOP file: "+std::string(file.disc_path));
    source.clear();source.seekg(static_cast<std::streamoff>(uint64_t(extent->lba)*2048));
    const auto output=DataChild(stage,file.path);
    fs::create_directories(output.parent_path());
    std::ofstream target(output,std::ios::binary);
    std::array<char,65536> block{};
    uint64_t remaining=file.bytes;
    while(remaining) {
      const auto count=static_cast<std::streamsize>(std::min<uint64_t>(remaining,block.size()));
      source.read(block.data(),count);target.write(block.data(),count);
      if(!source || !target)throw std::runtime_error("Cannot prepare disc file");
      remaining-=count;
    }
  }
  if(fs::file_size(image)!=bytes || fs::last_write_time(image)!=modified || !valid(stage))
    throw std::runtime_error("Disc preparation checksum failed");
  if(fs::exists(destination)) {
    // Preserve damaged or older caches as evidence; never merge them into a new
    // verified boot set or touch the user's original image.
    const auto retained=DataChild(paths.prepared,"previous-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    fs::rename(destination,retained);
  }
  fs::rename(stage,destination);
  return destination;
}
}
