#include "dodonpachi_data.h"
#include "common/catalogue.h"
#include "common/config_update.h"
#include "7z.h"
#include "7zCrc.h"
#include "7zFile.h"
extern "C" {
#include "inflate.h"
}
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <span>
#include <vector>

namespace cave::launcher {
namespace {
using Bytes = std::vector<uint8_t>;
using View = std::span<const uint8_t>;
struct Chip { const char* name; uint32_t crc, size; };
// MAME's ddonpach and ddonpachj (the same CRCs and sizes as Cave68kSDK).
constexpr Chip chips[] = {
    {"b1.u27",0xb5cdc8d3,0x80000}, {"b2.u26",0x6bbb063a,0x80000},
    {"u27.bin",0x2432ff9b,0x80000}, {"u26.bin",0x4f3a914a,0x80000},
    {"u50.bin",0x14b260ec,0x200000}, {"u51.bin",0xe7ba8cce,0x200000},
    {"u52.bin",0x02492ee0,0x200000}, {"u53.bin",0xcb4c10f0,0x200000},
    {"u60.bin",0x903096a7,0x200000}, {"u61.bin",0xd89b7631,0x200000},
    {"u62.bin",0x292bfb6b,0x200000}, {"u6.bin",0x9dfdafaf,0x200000},
    {"u7.bin",0x795b17d5,0x200000}, {"eeprom-ddonpach.bin",0x315fb546,0x80}
};
using Chips = std::array<Bytes,std::size(chips)>;
constexpr size_t max_archive = 64u << 20;
constexpr size_t max_solid_block = 96u << 20;

void Require(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}
bool IsSevenZip(const fs::path& path) { return _wcsicmp(path.extension().c_str(),L".7z")==0; }
bool IsZip(const fs::path& path) { return _wcsicmp(path.extension().c_str(),L".zip")==0; }
Bytes ReadFile(const fs::path& path, size_t limit) {
    const auto size = fs::file_size(path);
    Require(size <= limit, "DoDonPachi ROM file exceeds the supported size");
    Bytes bytes(static_cast<size_t>(size));
    std::ifstream file(path, std::ios::binary);
    Require(static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())),
            "Cannot read DoDonPachi ROM file");
    return bytes;
}
void WriteFile(const fs::path& path, View bytes) {
    std::ofstream file(path,std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    file.close();
}
size_t ChipIndex(uint32_t crc, uint64_t size) {
    for (size_t i=0; i<std::size(chips); ++i)
        if (chips[i].crc==crc && chips[i].size==size) return i;
    return std::size(chips);
}
uint32_t Little(View data, size_t at, unsigned count) {
    Require(at<=data.size() && count<=data.size()-at,"Truncated DoDonPachi ZIP");
    uint32_t value=0;
    for (unsigned i=0;i<count;++i) value |= uint32_t(data[at+i]) << (8*i);
    return value;
}
void ReadZip(const fs::path& path, Chips& found) {
    const Bytes bytes=ReadFile(path,max_archive);
    const View zip=bytes;
    Require(zip.size()>=22,"Invalid DoDonPachi ZIP");
    size_t end=zip.size()-22;
    while (Little(zip,end,4)!=0x06054b50 || end+22+Little(zip,end+20,2)!=zip.size()) {
        Require(end && zip.size()-end<65557,"Missing DoDonPachi ZIP directory");
        --end;
    }
    const auto count=Little(zip,end+10,2);
    size_t entry=Little(zip,end+16,4);
    for (unsigned n=0;n<count;++n) {
        Require(Little(zip,entry,4)==0x02014b50,"Invalid DoDonPachi ZIP directory");
        const auto method=Little(zip,entry+10,2), crc=Little(zip,entry+16,4);
        const auto packed=Little(zip,entry+20,4), size=Little(zip,entry+24,4);
        size_t local=Little(zip,entry+42,4);
        entry+=46+Little(zip,entry+28,2)+Little(zip,entry+30,2)+Little(zip,entry+32,2);
        const auto i=ChipIndex(crc,size);
        if (i==found.size() || !found[i].empty()) continue;
        Require(Little(zip,local,4)==0x04034b50,"Invalid DoDonPachi ZIP member");
        local+=30+Little(zip,local+26,2)+Little(zip,local+28,2);
        Require(local<=zip.size() && packed<=zip.size()-local,"Truncated DoDonPachi ZIP member");
        const auto input=zip.subspan(local,packed);
        Bytes output(size);
        if (method==0 && size==packed) std::copy(input.begin(),input.end(),output.begin());
        else Require(method==8 && inflate_raw(input.data(),input.size(),output.data(),output.size()),
                     "Unsupported or damaged DoDonPachi ZIP member");
        Require(CrcCalc(output.data(),output.size())==crc,"DoDonPachi ZIP chip CRC mismatch");
        found[i]=std::move(output);
    }
}

// The SDK allocates the solid block and dictionary before returning an entry.
// Limit their combined size, including allocations while reading the 7z header.
struct DecoderMemory {
    ISzAlloc api{Allocate,Free};
    mutable size_t available=192u<<20;
    struct alignas(std::max_align_t) Header { size_t size; };
    static void* Allocate(ISzAllocPtr allocator,size_t size) {
        auto& budget=reinterpret_cast<const DecoderMemory*>(allocator)->available;
        if (!size || size>budget) return nullptr;
        auto* p=static_cast<Header*>(std::malloc(sizeof(Header)+size));
        if (!p) return nullptr;
        budget-=size; p->size=size; return p+1;
    }
    static void Free(ISzAllocPtr allocator,void* data) {
        if (!data) return;
        auto* p=static_cast<Header*>(data)-1;
        reinterpret_cast<const DecoderMemory*>(allocator)->available+=p->size;
        std::free(p);
    }
};
struct SevenZip {
    DecoderMemory memory;
    CFileInStream stream{};
    CSzArEx db{};
    Byte* block=nullptr;
    SevenZip() { File_Construct(&stream.file); SzArEx_Init(&db); }
    ~SevenZip() {
        ISzAlloc_Free(&memory.api,block);
        SzArEx_Free(&db,&memory.api);
        File_Close(&stream.file);
    }
};
void ReadSevenZip(const fs::path& path, Chips& found) {
    SevenZip archive;
    Require(InFile_OpenW(&archive.stream.file,path.c_str())==0,"Cannot open DoDonPachi 7z file");
    FileInStream_CreateVTable(&archive.stream);
    CLookToRead2 look{};
    LookToRead2_CreateVTable(&look,False);
    std::array<Byte,1u<<15> input{};
    look.buf=input.data(); look.bufSize=input.size(); look.realStream=&archive.stream.vt;
    LookToRead2_INIT(&look)
    auto& db=archive.db;
    const auto* allocator=&archive.memory.api;
    Require(SzArEx_Open(&db,&look.vt,allocator,allocator)==SZ_OK && db.NumFiles<=4096,
            "Invalid or unsupported DoDonPachi 7z archive");
    UInt32 block_index=UINT32_MAX;
    size_t capacity=0;
    for (UInt32 n=0;n<db.NumFiles;++n) {
        const auto size=SzArEx_GetFileSize(&db,n);
        if (SzArEx_IsDir(&db,n) || (size!=0x80 && size!=0x80000 && size!=0x200000)) continue;
        if (SzBitWithVals_Check(&db.CRCs,n) && ChipIndex(db.CRCs.Vals[n],size)==found.size()) continue;
        const auto folder=db.FileToFolder[n];
        Require(folder<db.db.NumFolders && SzAr_GetFolderUnpackSize(&db.db,folder)<=max_solid_block,
                "DoDonPachi 7z block exceeds the supported size");
        size_t offset=0, unpacked=0;
        Require(SzArEx_Extract(&db,&look.vt,n,&block_index,&archive.block,&capacity,&offset,&unpacked,
                               allocator,allocator)==SZ_OK && unpacked==size,
                "Cannot decode DoDonPachi 7z member");
        const auto* data=archive.block+offset;
        const auto i=ChipIndex(CrcCalc(data,unpacked),unpacked);
        if (i!=found.size() && found[i].empty()) found[i].assign(data,data+unpacked);
    }
}
void ReadArchive(const fs::path& path,Chips& found) {
    Require(fs::file_size(path)<=max_archive,"DoDonPachi ROM archive exceeds the supported size");
    static std::once_flag init;
    std::call_once(init,CrcGenerateTable);
    if (IsSevenZip(path)) ReadSevenZip(path,found);
    else ReadZip(path,found);
}
bool World(const Chips& set) { return !set[0].empty() && !set[1].empty(); }
bool Japan(const Chips& set) { return !set[2].empty() && !set[3].empty(); }
bool CommonChips(const Chips& set) {
    return std::all_of(set.begin()+4,set.end(),[](const Bytes& data){return !data.empty();});
}
struct RomDirectory { fs::path path; std::string edition; };

RomDirectory PrepareArchive(const fs::path& source,const fs::path& prepared) {
    // A split Japanese set needs the parent's common chips. A World set can
    // also supply the Japanese option when a sibling clone is present.
    std::vector<fs::path> companions;
    std::string key=Sha256(source);
    for (const auto name : {L"ddonpach.zip",L"ddonpach.7z",L"ddonpachj.zip",L"ddonpachj.7z"}) {
        const auto sibling=source.parent_path()/name;
        if (!fs::is_regular_file(sibling) || fs::equivalent(source,sibling)) continue;
        if (fs::file_size(sibling)>max_archive) continue;
        companions.push_back(sibling);
        key+="-"+Sha256(sibling).substr(0,12);
    }
    const auto dir=DataChild(prepared,key);
    const auto marker=dir/"edition.txt";
    if (fs::is_regular_file(marker)) {
        const auto edition=Ini::ReadText(marker);
        if (edition=="world" || edition=="japan" || edition=="both") return {dir,edition};
    }
    Chips set{};
    ReadArchive(source,set);
    Require(World(set)||Japan(set),"Neither World nor Japan DoDonPachi program chips were found in the selected archive");
    const std::string edition=World(set)?(Japan(set)?"both":"world"):"japan";
    for (const auto& sibling:companions) {
        if (CommonChips(set) && (edition=="japan" || Japan(set))) break;
        if (CommonChips(set) && _wcsicmp(sibling.stem().c_str(),L"ddonpachj")!=0) continue;
        Chips extra{};
        try { ReadArchive(sibling,extra); }
        catch (const std::runtime_error&) { continue; }
        for (size_t i=0;i<set.size();++i)
            if (set[i].empty()) set[i]=std::move(extra[i]);
    }
    Require(CommonChips(set),"Missing DoDonPachi graphics, sound or EEPROM. Place the parent ddonpach.zip or ddonpach.7z next to the split set.");
    fs::create_directories(prepared);
    const auto stage=DataChild(prepared,".import-"+std::to_string(std::random_device{}()));
    Require(fs::create_directory(stage),"Cannot create DoDonPachi import directory");
    try {
        for (size_t i=0;i<set.size();++i) if (!set[i].empty()) WriteFile(stage/chips[i].name,set[i]);
        WriteFile(stage/"edition.txt",{reinterpret_cast<const uint8_t*>(edition.data()),edition.size()});
        std::error_code error;
        fs::rename(stage,dir,error);
        if (error && !fs::is_regular_file(marker)) throw fs::filesystem_error("Publish DoDonPachi chips",dir,error);
    } catch (...) {
        std::error_code ignored; fs::remove_all(stage,ignored); throw;
    }
    std::error_code ignored; fs::remove_all(stage,ignored);
    return {dir,edition};
}
}

fs::path FindDoDonPachiSource(const fs::path& data) {
    if (fs::is_regular_file(data) && (IsZip(data)||IsSevenZip(data))) return data;
    if (!fs::is_directory(data)) return {};
    for (const auto name : {"ddonpach.zip","ddonpach.7z","ddonpachj.zip","ddonpachj.7z"})
        if (const auto path=DataChild(data,name); fs::is_regular_file(path)) return path;
    if (fs::is_regular_file(data/"b1.u27") || fs::is_regular_file(data/"u27.bin")) return data;
    return {};
}

void ConfigureDoDonPachi(const fs::path& source,const fs::path& prepared,
                         const fs::path& program,const fs::path& settings) {
    RomDirectory rom;
    if (source.empty()) {
        rom={program,fs::is_regular_file(program/"ddonpach.dat")?
            (fs::is_regular_file(program/"ddonpach_jpn.dat")?"both":"world"):"japan"};
    } else if (fs::is_directory(source)) {
        const bool world=fs::is_regular_file(source/"b1.u27");
        rom={source,world?(fs::is_regular_file(source/"u27.bin")?"both":"world"):"japan"};
    } else {
        Require(fs::file_size(source)<=max_archive,"DoDonPachi ROM archive exceeds the supported size");
        rom=PrepareArchive(source,prepared);
    }
    std::optional<std::string> before;
    if (fs::exists(settings)) before=Ini::ReadText(settings);
    const auto ini=Ini::Parse(before.value_or(""));
    const auto path=Utf8(rom.path);
    const auto* previous=ini.Get("files","rom");
    const bool changed=!previous || previous->text!=path;
    const auto* version=ini.Get("game","version");
    const bool japan=version && _stricmp(version->text.c_str(),"japan")==0;
    const bool selected_available=fs::is_regular_file(program/(japan?"ddonpach_jpn.dat":"ddonpach.dat"));
    std::vector<IniEdit> edits;
    if (changed) edits.push_back({"files","rom",path});
    if (rom.edition!="both" && (changed || !selected_available) && (!version || version->text!=rom.edition))
        edits.push_back({"game","version",rom.edition});
    if (!edits.empty()) UpdateIniFile(settings,before?std::optional<std::string_view>(*before):std::nullopt,edits);
}
}
