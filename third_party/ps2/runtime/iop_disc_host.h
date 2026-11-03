#pragma once
#include <cstdint>
#include <string_view>
namespace cave::ps2 {
// Optional ISO metadata boundary for IOP services. Search and raw-sector reads
// must address the same disc, even when only small boot files are cached.
class IopDiscHost {
public:
    virtual ~IopDiscHost() = default;
    virtual bool FindDiscFile(std::string_view path,uint32_t& lba,uint32_t& size) const = 0;
};
}
