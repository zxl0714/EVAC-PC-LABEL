#include "plain_self.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace cave::ps4 {
PreparationBytes PlainSelfElf(std::span<const std::uint8_t> source) {
    constexpr std::array<std::uint8_t,4> magic{0x4f,0x15,0x3d,0x1d};
    if (source.size()<4 || !std::equal(magic.begin(),magic.end(),source.begin()))
        return {source.begin(),source.end()};
    const auto require=[](bool okay) {if (!okay) throw std::runtime_error("Unsupported or malformed plaintext SELF");};
    const auto value=[&](std::uint64_t offset,std::size_t bytes) {
        require(offset<=source.size() && bytes<=source.size()-offset);
        std::uint64_t result{};
        for (std::size_t i=0;i<bytes;++i) result|=std::uint64_t(source[static_cast<std::size_t>(offset)+i])<<(8*i);
        return result;
    };
    constexpr std::array<std::uint8_t,6> profile{0,1,1,0x12,1,1};
    require(source.size()>=32 && source.size()<=512ull*1024*1024 &&
        std::equal(profile.begin(),profile.end(),source.begin()+4));
    const auto declared=value(16,8),count=value(24,2),header=value(12,2),meta=value(14,2),embedded=32+count*32;
    require(count && count<=64 && embedded+64<=header && header+meta<=declared && declared<=source.size());
    constexpr std::array<std::uint8_t,7> elf_magic{0x7f,'E','L','F',2,1,1};
    require(std::equal(elf_magic.begin(),elf_magic.end(),source.begin()+embedded));
    const auto phoff=value(embedded+32,8),phnum=value(embedded+56,2);
    require(value(embedded+54,2)==56 && phnum && phnum<=64 && phoff>=64 && phoff<=65536);
    const auto table_end=phoff+56*phnum;
    require(embedded+table_end<=header);
    struct Program {std::uint64_t offset,size,kind;};
    std::vector<Program> programs;
    auto extent=table_end;
    for (std::uint64_t i=0;i<phnum;++i) {
        const auto at=embedded+phoff+56*i;
        const Program program{value(at+8,8),value(at+32,8),value(at,4)};
        require(program.offset<=512ull*1024*1024 && program.size<=512ull*1024*1024-program.offset);
        extent=(std::max)(extent,program.offset+program.size);
        programs.push_back(program);
    }
    PreparationBytes result(static_cast<std::size_t>(extent));
    std::copy_n(source.begin()+embedded,table_end,result.begin());
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges{{0,table_end}};
    std::set<std::uint64_t> selected;
    for (std::uint64_t i=0;i<count;++i) {
        const auto at=32+i*32,flags=value(at,8),offset=value(at+8,8),size=value(at+16,8),memory=value(at+24,8);
        require(!(flags&10) && offset>=header+meta && offset<=declared && size<=declared-offset);
        if (!(flags&0x800)) continue;
        const auto id=(flags>>20)&0xfff;
        require(id<programs.size() && selected.insert(id).second);
        const auto& program=programs[static_cast<std::size_t>(id)];
        const auto target=program.offset,length=program.size;
        require(size==memory && size==length && target<=extent && length<=extent-target);
        for (const auto& [began,ended]:ranges) {
            const auto start=(std::max)(began,target),stop=(std::min)(ended,target+length);
            require(stop<=start || std::equal(result.begin()+start,result.begin()+stop,source.begin()+offset+start-target));
        }
        std::copy_n(source.begin()+offset,size,result.begin()+target);
        ranges.emplace_back(target,target+length);
    }
    if (source.size()>declared) {
        const Program* note{};
        for (const auto& program:programs) if (program.kind==0x6fffff01 && program.size) {
            require(!note);note=&program;
        }
        require(note && source.size()-declared<=4096 && note->size==source.size()-declared);
        for (const auto& [start,stop]:ranges) require(note->offset+note->size<=start || note->offset>=stop);
        std::copy(source.begin()+declared,source.end(),result.begin()+note->offset);
        ranges.emplace_back(note->offset,note->offset+note->size);
    }
    std::sort(ranges.begin(),ranges.end());
    for (const auto& program:programs) {
        auto cursor=program.offset;
        for (const auto& [start,end]:ranges) if (start<=cursor) cursor=(std::max)(cursor,end);
        require(cursor>=program.offset+program.size);
    }
    return result;
}
}
