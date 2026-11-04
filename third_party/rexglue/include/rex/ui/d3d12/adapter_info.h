#pragma once
#include <cstdint>
#include <optional>

namespace rex::ui::d3d12 {
struct AdapterInfo {
  uint32_t vendor;
  uint32_t device;
  uint64_t luid;
};
std::optional<AdapterInfo> QuerySelectedAdapter();
}
