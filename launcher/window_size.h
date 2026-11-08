#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cave::launcher {
inline constexpr int kHeaderHeight=110,kFooterHeight=120,kCardGap=12;
inline constexpr int kMinimumCardHeight=92,kDefaultCardHeight=108;
inline constexpr float kCoverColumnGap=24,kCoverRowGap=28,kCoverInset=10;
inline constexpr float kCoverHeaderHeight=120,kCoverPreferredWidth=192;

inline float CoverMargin(float width) {
  return std::max(width<800?24.f:36.f,(width-1320.f)/2);
}
inline std::size_t CoverColumns(float width) {
  return std::clamp(std::size_t(std::max(0.f,width-2*CoverMargin(width)+kCoverColumnGap)/
                                (184.f+kCoverColumnGap)),std::size_t(1),std::size_t(6));
}
inline float CoverWidth(float width) {
  const auto columns=CoverColumns(width);
  return std::max(1.f,(width-2*CoverMargin(width)-kCoverColumnGap*float(columns-1))/float(columns));
}
inline float CoverHeight(float width) {
  // A 3:4 artwork well, then a two-line title and a separate subtitle baseline.
  return std::max(1.f,width-2*kCoverInset)*4.f/3.f+84.f;
}

struct WindowSize {int width,height;};

inline WindowSize RestoreWindowSize(WindowSize saved,int work_width,int work_height,unsigned dpi) {
  const auto scale=double(dpi?dpi:96)/96;
  return {std::clamp(int(std::lround(std::max(720,saved.width)*scale)),1,std::max(1,work_width)),
          std::clamp(int(std::lround(std::max(520,saved.height)*scale)),1,std::max(1,work_height))};
}

inline WindowSize InitialCoverWindowSize(int work_width,int work_height,unsigned dpi,
                                        int frame_width,int frame_height,std::size_t games) {
  const auto scale=float(dpi?dpi:96)/96;
  const auto width_limit=std::max(1,int(static_cast<long long>(work_width)*9/10));
  const auto height_limit=std::max(1,int(static_cast<long long>(work_height)*9/10));
  auto columns=std::size_t(4);
  const auto desired_width=[&] {
    const auto client=std::max(720.f,72.f+kCoverPreferredWidth*float(columns)+kCoverColumnGap*float(columns-1));
    return int(std::ceil(client*scale))+frame_width;
  };
  while(columns>1 && desired_width()>width_limit)--columns;
  const auto width=std::clamp(desired_width(),1,width_limit);
  const auto client_width=std::max(1.f,float(width-frame_width)/scale);
  const auto actual_columns=CoverColumns(client_width);
  const auto rows=(games+actual_columns-1)/actual_columns;
  const auto content=float(rows)*CoverHeight(CoverWidth(client_width))+
                     (rows?float(rows-1)*kCoverRowGap:0.f);
  const auto client_height=std::max(520.f,kCoverHeaderHeight+kFooterHeight+content);
  const auto height=int(std::ceil(client_height*scale))+frame_height;
  // Show the whole library if it fits with a small desktop margin. Otherwise scroll.
  const auto full_limit=std::max(1,work_height-int(std::ceil(24*scale)));
  return {width,height<=full_limit?height:std::clamp(height,1,height_limit)};
}

// Layout units are 96-DPI pixels; the monitor work area is in physical pixels.
// Leave desktop space above and below the initial window; extra cards scroll.
inline WindowSize InitialWindowSize(int current_width,int work_width,int work_height,
                                   unsigned dpi,int frame_height,std::size_t games) {
  const auto rows=int(games);
  const auto client_height=kHeaderHeight+kFooterHeight+rows*kDefaultCardHeight+
                           std::max(0,rows-1)*kCardGap;
  const auto height=int((client_height*static_cast<long long>(dpi?dpi:96)+95)/96)+frame_height;
  const auto height_limit=std::max(1,int(static_cast<long long>(work_height)*9/10));
  return {std::clamp(current_width,1,std::max(1,work_width)),
          std::clamp(height,1,height_limit)};
}
}
