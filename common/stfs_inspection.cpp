#include "common/stfs.h"
#include <algorithm>
#include <exception>

namespace cave {
namespace {
std::string Lower(std::string text) {
  for(auto& c:text)if(c>='A' && c<='Z')c=char(c-'A'+'a');return text;
}
}
ContentInspection InspectStfs(const ContentPackage& package,const fs::path& file,CheckLevel level) {
  ContentInspection result;result.id=package.id;result.name=package.name;
  try {
    StfsPackage archive(file,level==CheckLevel::Full);
    if(archive.TitleId()!=package.title_id)return result;
    unsigned present=0;
    for(const auto& expected:package.files)for(const auto& entry:archive.Files())
      if(Lower(entry.path)==Lower(Utf8(ContentRelativePath(package,expected))))++present;
    if(!present)return result;
    result.directory=file.parent_path();result.container=file;
    for(const auto& expected:package.files) {
      ++result.files_checked;const auto relative=Utf8(ContentRelativePath(package,expected));
      const auto entry=std::find_if(archive.Files().begin(),archive.Files().end(),[&](const auto& item){return Lower(item.path)==Lower(relative);});
      if(entry==archive.Files().end()){result.problems.push_back("missing: "+relative);continue;}
      if(entry->bytes!=expected.bytes){result.problems.push_back("size: "+relative);continue;}
      if(level==CheckLevel::Full){++result.hashes_checked;if(entry->sha256!=expected.sha256)result.problems.push_back("sha256: "+relative);}
    }
    if(archive.Files().size()!=package.files.size())result.problems.push_back("Unexpected DLC payload files");
    result.state=result.problems.empty()?(level==CheckLevel::Full?DataState::Verified:DataState::Unverified):DataState::Incomplete;
  }catch(const std::exception& error) {
    if(file.filename()==FromUtf8(package.package_name)){result.directory=file.parent_path();result.container=file;result.state=DataState::Incomplete;result.problems.push_back(error.what());}
  }
  return result;
}
}
