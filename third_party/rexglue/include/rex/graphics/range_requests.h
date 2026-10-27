#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace rex::graphics {

// RequestRange overwhelmingly supplies one range. Borrow it for this call;
// retain sorting and merging for genuine multi-range requests.
class RangeRequests {
 public:
  using Range = std::pair<uint32_t, uint32_t>;
  RangeRequests() = default;
  RangeRequests(const RangeRequests&) = delete;
  RangeRequests& operator=(const RangeRequests&) = delete;

  bool Prepare(const Range* ranges, size_t count, uint32_t limit) {
    view_ = {};
    storage_.clear();
    if (!ranges || !count) return true;
    if (count == 1) {
      if (!ranges->second) return true;
      if (ranges->first > limit || ranges->second > limit - ranges->first) return false;
      view_ = {ranges, 1};
      return true;
    }
    storage_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      const auto& range = ranges[i];
      if (!range.second) continue;
      if (range.first > limit || range.second > limit - range.first) return false;
      storage_.push_back(range);
    }
    if (storage_.empty()) return true;
    std::sort(storage_.begin(), storage_.end(), [](const Range& a, const Range& b) {
      return a.first < b.first;
    });
    size_t last = 0;
    for (size_t i = 1; i < storage_.size(); ++i) {
      auto& previous = storage_[last];
      const auto current = storage_[i];
      const uint64_t previous_end = uint64_t(previous.first) + previous.second;
      if (current.first <= previous_end) {
        const uint64_t current_end = uint64_t(current.first) + current.second;
        if (current_end > previous_end) previous.second = uint32_t(current_end - previous.first);
      } else {
        storage_[++last] = current;
      }
    }
    storage_.resize(last + 1);
    view_ = storage_;
    return true;
  }

  std::span<const Range> view() const { return view_; }

 private:
  std::vector<Range> storage_;
  std::span<const Range> view_;
};

}  // namespace rex::graphics
