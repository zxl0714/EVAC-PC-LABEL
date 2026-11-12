#pragma once
#include "common/catalogue.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cave::ps4 {
// Keep the actual source digest for file binding. Only patch selection uses
// this separately verified identity when a known program has another wrapper.
inline std::array<std::uint8_t,32> ProgramContract(
    const std::array<std::uint8_t,32>& source,const DataFile* expected,std::string_view fingerprint) {
    if(!expected)return source;
    std::string raw;
    for(auto byte:source){raw+="0123456789abcdef"[byte>>4];raw+="0123456789abcdef"[byte&15];}
    if(raw==expected->sha256)return source;
    if(!expected->program || expected->program_fingerprint.empty() ||
       fingerprint!=expected->program_fingerprint || expected->sha256.size()!=64)
        throw std::runtime_error("Program differs from the verified patch contract");
    const auto hex=[](char c)->unsigned {
        if(c>='0' && c<='9')return c-'0';
        if(c>='a' && c<='f')return c-'a'+10;
        throw std::runtime_error("Invalid program contract digest");
    };
    std::array<std::uint8_t,32> result{};
    for(std::size_t i=0;i<result.size();++i)
        result[i]=static_cast<std::uint8_t>((hex(expected->sha256[2*i])<<4)|hex(expected->sha256[2*i+1]));
    return result;
}
}
