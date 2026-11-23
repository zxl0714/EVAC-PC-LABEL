#include "disc_image.h"
#include <libchdr/chd.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cave::ps2 {
bool DiscImage::IsChd(const std::filesystem::path& path) {
  return _wcsicmp(path.extension().c_str(),L".chd")==0;
}

class DiscImage::Buffer final : public std::streambuf {
 public:
  explicit Buffer(const std::filesystem::path& path)
      : file_(_wfopen(path.c_str(),L"rb"),std::fclose), chd_(nullptr,chd_close) {
    if(!file_)throw std::runtime_error("Cannot open disc image");
    if(!IsChd(path)) {
      size=std::filesystem::file_size(path);
      return;
    }
    chd_file* handle=nullptr;
    const auto error=chd_open_file(file_.get(),CHD_OPEN_READ,nullptr,&handle);
    if(error!=CHDERR_NONE)throw std::runtime_error(std::string("Cannot open CHD: ")+chd_error_string(error));
    chd_.reset(handle);
    const auto& h=*chd_get_header(handle);
    if(!h.hunkbytes || h.hunkbytes>1024*1024 || !h.logicalbytes ||
       h.logicalbytes>uint64_t(std::numeric_limits<int64_t>::max()))
      throw std::runtime_error("Invalid CHD geometry");
    hunk_.resize(h.hunkbytes);
    if(h.unitbytes==2048) {
      size=h.logicalbytes;
      return;
    }
    if(h.unitbytes!=2448)throw std::runtime_error("Unsupported CHD optical sector format");
    std::array<char,512> track{};
    uint32_t length{};
    auto tag=CDROM_TRACK_METADATA2_TAG;
    if(chd_get_metadata(handle,tag,0,track.data(),track.size()-1,&length,nullptr,nullptr)==CHDERR_METADATA_NOT_FOUND)
      tag=CDROM_TRACK_METADATA_TAG;
    if(chd_get_metadata(handle,tag,0,track.data(),track.size()-1,&length,nullptr,nullptr)!=CHDERR_NONE ||
       length>=track.size() || chd_get_metadata(handle,tag,1,nullptr,0,nullptr,nullptr,nullptr)!=CHDERR_METADATA_NOT_FOUND)
      throw std::runtime_error("Expected one CHD data track");
    char type[32]{},sub[32]{};
    unsigned index{},frames{};
    if(std::sscanf(track.data(),"TRACK:%u TYPE:%31s SUBTYPE:%31s FRAMES:%u",&index,type,sub,&frames)!=4 ||
       index!=1 || !frames || uint64_t(frames)*h.unitbytes>h.logicalbytes)
      throw std::runtime_error("Invalid CHD track metadata");
    const std::string_view mode(type);
    if(mode=="MODE1_RAW")prefix_=16;
    else if(mode=="MODE2_RAW")prefix_=24;
    else if(mode=="MODE2")prefix_=8;
    else if(mode!="MODE1" && mode!="MODE2_FORM1")throw std::runtime_error("Unsupported CHD data track");
    stride_=h.unitbytes;
    size=uint64_t(frames)*2048;
  }
  uint64_t size{};
 private:
  pos_type seekpos(pos_type position,std::ios_base::openmode mode) override {
    return seekoff(off_type(position),std::ios_base::beg,mode);
  }
  pos_type seekoff(off_type offset,std::ios_base::seekdir dir,std::ios_base::openmode) override {
    const auto base=int64_t(dir==std::ios_base::end?size:dir==std::ios_base::cur?position_:0);
    if(offset < -base || offset>int64_t(size)-base)return pos_type(-1);
    const uint64_t next=base+offset;
    if(!chd_ && next!=position_ && _fseeki64(file_.get(),next,SEEK_SET))return pos_type(-1);
    position_=next;
    return pos_type(next);
  }
  std::streamsize xsgetn(char* dest,std::streamsize requested) override {
    const auto bytes=std::min<uint64_t>(requested,size-position_);
    if(!chd_) {
      const auto read=std::fread(dest,1,size_t(bytes),file_.get());
      position_+=read;
      return read;
    }
    uint64_t done=0;
    while(done<bytes) {
      const auto physical=(position_/2048)*stride_+prefix_+position_%2048;
      const auto hunk=uint32_t(physical/hunk_.size());
      const auto within=size_t(physical%hunk_.size());
      if(cached_!=hunk) {
        if(chd_read(chd_.get(),hunk,hunk_.data())!=CHDERR_NONE)break;
        cached_=hunk;
      }
      auto take=std::min<uint64_t>(bytes-done,hunk_.size()-within);
      if(stride_!=2048)take=std::min(take,2048-position_%2048);
      std::memcpy(dest+done,hunk_.data()+within,size_t(take));
      position_+=take;
      done+=take;
    }
    return std::streamsize(done);
  }
  std::unique_ptr<std::FILE,decltype(&std::fclose)> file_;
  std::unique_ptr<chd_file,decltype(&chd_close)> chd_;
  std::vector<uint8_t> hunk_;
  uint64_t position_{};
  uint32_t cached_=UINT32_MAX,stride_=2048,prefix_{};
};

DiscImage::DiscImage(const std::filesystem::path& path):std::istream(nullptr),buffer_(std::make_unique<Buffer>(path)) {
  rdbuf(buffer_.get());
}
DiscImage::~DiscImage()=default;
uint64_t DiscImage::Size() const {return buffer_->size;}
}
