#pragma once
#include <algorithm>
#include <cstddef>

namespace cave::launcher {
inline constexpr int kHeaderHeight=150,kFooterHeight=120,kCardGap=12;
inline constexpr int kMinimumCardHeight=92,kDefaultCardHeight=108;

struct WindowSize {int width,height;};

// Layout units are 96-DPI pixels; the monitor work area is in physical pixels.
inline WindowSize InitialWindowSize(int current_width,int work_width,int work_height,
                                   unsigned dpi,int frame_height,std::size_t games) {
  const auto rows=int(games);
  const auto client_height=kHeaderHeight+kFooterHeight+rows*kDefaultCardHeight+
                           std::max(0,rows-1)*kCardGap;
  const auto height=int((client_height*static_cast<long long>(dpi?dpi:96)+95)/96)+frame_height;
  return {std::clamp(current_width,1,std::max(1,work_width)),
          std::clamp(height,1,std::max(1,work_height))};
}
}
