#include "common/ps4_program.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

#include <Windows.h>
#include <bcrypt.h>
#include <fstream>

namespace cave::ps4 {
std::vector<std::uint8_t> PlainSelfElf(std::span<const std::uint8_t> source) {
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
    std::vector<std::uint8_t> result(static_cast<std::size_t>(extent));
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

namespace cave::ps4 {
namespace {
constexpr std::uint64_t kProgramLimit=512ull*1024*1024;
std::uint64_t LittleEndian(std::span<const std::uint8_t> bytes,std::size_t at,std::size_t size) {
    std::uint64_t value{};
    for (std::size_t i=0;i<size;++i) value|=std::uint64_t(bytes[at+i])<<(i*8);
    return value;
}
struct ProgramHash {
    BCRYPT_HASH_HANDLE handle{};
    ProgramHash() {
        if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&handle,nullptr,0,nullptr,0,0)<0)
            throw std::runtime_error("Cannot initialize PS4 program SHA-256");
    }
    ~ProgramHash() {BCryptDestroyHash(handle);}
    void Add(std::span<const std::uint8_t> bytes) {
        if (!bytes.empty() && BCryptHashData(handle,const_cast<std::uint8_t*>(bytes.data()),static_cast<ULONG>(bytes.size()),0)<0)
            throw std::runtime_error("Cannot hash PS4 program extent");
    }
    std::string Finish() {
        std::array<std::uint8_t,32> digest{};
        if (BCryptFinishHash(handle,digest.data(),static_cast<ULONG>(digest.size()),0)<0)
            throw std::runtime_error("Cannot finish PS4 program SHA-256");
        constexpr char hex[]="0123456789abcdef";
        std::string result;
        result.reserve(digest.size()*2);
        for (const auto byte:digest) {result+=hex[byte>>4];result+=hex[byte&15];}
        return result;
    }
};

// Firmware backports change the SDK word in the process/module parameters
// and the version at the end of each length-prefixed SCE library note.
// Parameter bytes are also covered by PT_LOAD, so use file offsets for both.
std::vector<std::size_t> SdkWords(std::span<const std::uint8_t> source,
                                  std::span<const std::uint8_t> programs) {
    std::vector<std::size_t> words;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> executable;
    for (std::size_t at=0;at<programs.size();at+=56) {
        const auto kind=LittleEndian(programs,at,4),begin=LittleEndian(programs,at+8,8);
        const auto size=LittleEndian(programs,at+32,8);
        if (begin>source.size() || size>source.size()-begin) continue;
        const auto segment=source.subspan(begin,size);
        if (kind==1 && (LittleEndian(programs,at+4,4)&1) && size) {
            executable.emplace_back(begin,begin+size);
        } else if (kind==0x61000001 || kind==0x61000002) {
            const auto magic=kind==0x61000001 ? 0x4942524fu : 0x3c13f4bfu;
            if (size<24) continue;
            // libc can place more data after the parameter structure in the same segment.
            const auto parameter_size=LittleEndian(segment,0,8);
            if (parameter_size>=24 && parameter_size<=size && LittleEndian(segment,8,4)==magic)
                words.push_back(begin+16);
        } else if (kind==0x6fffff01 && size<=65536) {
            const auto previous=words.size();
            for (std::size_t pos=0;pos<size;) {
                const auto length=segment[pos++];
                if (length<6 || length>size-pos || segment[pos+length-5]!=':') {
                    words.resize(previous);
                    break;
                }
                pos+=length;
                words.push_back(begin+pos-4);
            }
        }
    }
    std::erase_if(words,[&](std::size_t word) {
        return std::ranges::any_of(executable,[word](const auto& range) {
            return word<range.second && range.first<word+4;
        });
    });
    std::ranges::sort(words);
    return words;
}
}
std::string ProgramFingerprint(std::span<const std::uint8_t> source) {
    constexpr std::array<std::uint8_t,4> self_magic{0x4f,0x15,0x3d,0x1d};
    constexpr std::array<std::uint8_t,7> elf_magic{0x7f,'E','L','F',2,1,1};
    if (source.size()<64 || source.size()>kProgramLimit)return {};
    std::vector<std::uint8_t> decoded;
    if (std::ranges::equal(source.first(4),self_magic)) {
        try {decoded=PlainSelfElf(source);}
        catch (const std::runtime_error&) {return {};}
        source=decoded;
    }
    if (source.size()<64 || !std::ranges::equal(source.first(7),elf_magic) ||
        LittleEndian(source,18,2)!=62 || LittleEndian(source,20,4)!=1 ||
        LittleEndian(source,52,2)!=64 || LittleEndian(source,54,2)!=56)return {};
    const auto table=LittleEndian(source,32,8),count=LittleEndian(source,56,2);
    if (table<64 || table>65536 || table>source.size() || !count || count>64 || 56*count>source.size()-table)
        return {};
    std::array<std::uint8_t,64> header{};
    std::ranges::copy(source.first(64),header.begin());
    // Section headers are not used to load the executable. Authenticate the
    // rest of the ELF header, every program header and its file-backed bytes
    // apart from recognized, non-executable SDK version fields.
    std::fill(header.begin()+40,header.begin()+48,0);
    std::fill(header.begin()+58,header.end(),0);
    constexpr std::uint8_t domain[]="EVAC PS4 program v1";
    ProgramHash hash;
    hash.Add(domain);
    hash.Add(header);
    const auto programs=source.subspan(static_cast<std::size_t>(table),static_cast<std::size_t>(56*count));
    hash.Add(programs);
    const auto sdk=SdkWords(source,programs);
    constexpr std::array<std::uint8_t,4> zero{};
    for (std::size_t index=0;index<count;++index) {
        const auto at=index*56,offset=LittleEndian(programs,at+8,8),bytes=LittleEndian(programs,at+32,8);
        if (offset>source.size() || bytes>source.size()-offset)return {};
        auto cursor=offset;
        const auto end=offset+bytes;
        for (const auto word:sdk) {
            if (word>=end) break;
            if (word+4<=cursor) continue;
            const auto first=(std::max)(cursor,static_cast<std::uint64_t>(word));
            hash.Add(source.subspan(cursor,first-cursor));
            cursor=(std::min)(end,static_cast<std::uint64_t>(word+4));
            hash.Add(std::span(zero).first(cursor-first));
        }
        hash.Add(source.subspan(cursor,end-cursor));
    }
    return hash.Finish();
}
std::string ProgramFingerprint(const fs::path& path) {
    const auto size=fs::file_size(path);
    if (size<64 || size>kProgramLimit)return {};
    std::vector<std::uint8_t> source(static_cast<std::size_t>(size));
    std::ifstream file(path,std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(source.data()),static_cast<std::streamsize>(size)) ||
        file.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("PS4 program changed during verification");
    return ProgramFingerprint(source);
}
}
