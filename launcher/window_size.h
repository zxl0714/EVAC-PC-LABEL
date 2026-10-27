#pragma once
#include <algorithm>
#include <cstddef>

namespace cave::launcher {
inline constexpr int kHeaderHeight=110,kFooterHeight=120,kCardGap=12;
inline constexpr int kMinimumCardHeight=92,kDefaultCardHeight=108;

struct WindowSize {int width,height;};

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
