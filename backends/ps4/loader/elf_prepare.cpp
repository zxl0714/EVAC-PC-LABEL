// Native equivalent of scripts/ps4/{native_elf,prepare_native}.py.
#include "elf_prepare.h"
#include "plain_self.h"
#include "esprade_latency.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

namespace cave::ps4 {
namespace {
constexpr std::uint64_t kLimit = 512ull*1024*1024;
constexpr std::uint64_t kStrings=0x61000035, kStringBytes=0x61000037,
    kSymbols=0x61000039, kSymbolBytes=0x6100003f;
void Require(bool okay, const char* error) { if (!okay) throw std::runtime_error(error); }
std::array<std::uint8_t,32> Hash(std::span<const std::uint8_t> source) {
    BCRYPT_ALG_HANDLE algorithm{};
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot open SHA-256");
    std::array<std::uint8_t,32> result{};
    const auto status=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(source.data()),
        static_cast<ULONG>(source.size()),result.data(),static_cast<ULONG>(result.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    Require(status>=0,"Cannot hash ELF input");
    return result;
}
struct Header { std::uint32_t kind{},flags{}; std::uint64_t offset{},address{},file_size{},memory_size{},alignment{}; };
struct Symbol { std::string name; std::uint8_t type{},binding{}; std::uint16_t section{}; std::uint64_t value{},size{}; };
struct Relocation { std::uint64_t address{},symbol{}; std::uint32_t type{}; std::int64_t addend{}; };
struct Identity { std::string name,friendly,library,module; std::size_t index{}; };
std::string EncodeId(std::uint64_t value) {
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string result;
    do { result.insert(result.begin(),alphabet[value&63]); value>>=6; } while (value);
    return result;
}
class Elf {
    std::span<const std::uint8_t> raw_,strings_;
    Header dyn_data_;
    std::map<std::string,std::string> libraries_,modules_;
public:
    std::vector<Header> segments;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> dynamic;
    std::map<std::uint64_t,std::uint64_t> tags;
    std::uint64_t entry{},image_size{},procparam{},tls_bytes{},unwind_address{},unwind_size{};
    bool unwind_present{};
    std::uint16_t type{};
    std::span<const std::uint8_t> Slice(std::uint64_t offset,std::uint64_t size) const {
        Require(offset<=raw_.size() && size<=raw_.size()-offset,"ELF file extent is out of bounds");
        return raw_.subspan(static_cast<std::size_t>(offset),static_cast<std::size_t>(size));
    }
    template<typename T> T Value(std::uint64_t offset) const {
        using U=std::make_unsigned_t<T>;
        const auto data=Slice(offset,sizeof(T)); U value{};
        for (std::size_t i=0;i<sizeof(T);++i) value|=U(data[i])<<(i*8);
        return std::bit_cast<T>(value);
    }
    bool Contains(std::uint64_t address,std::uint64_t size,bool executable=false) const {
        return std::ranges::any_of(segments,[&](const auto& segment) {
            const auto extent=executable?segment.file_size:segment.memory_size;
            return (!executable || (segment.flags&1)) && address>=segment.address &&
                size<=extent && address-segment.address<=extent-size;
        });
    }
    std::span<const std::uint8_t> DynamicSlice(std::uint64_t offset,std::uint64_t size) const {
        Require(offset<=dyn_data_.file_size && size<=dyn_data_.file_size-offset,"ELF dynamic extent is out of bounds");
        return Slice(dyn_data_.offset+offset,size);
    }
    std::string String(std::uint64_t offset) const {
        Require(offset<strings_.size(),"ELF string offset is out of bounds");
        const auto rest=strings_.subspan(static_cast<std::size_t>(offset));
        const auto end=std::find(rest.begin(),rest.end(),0);
        Require(end!=rest.end() && end-rest.begin()<=4096,"Unterminated or oversized ELF string");
        std::string value(reinterpret_cast<const char*>(rest.data()),static_cast<std::size_t>(end-rest.begin()));
        Require(ValidUtf8(value),"Invalid UTF-8 ELF string");
        return value;
    }
    explicit Elf(std::span<const std::uint8_t> raw):raw_(raw) {
        Require(raw.size()<=kLimit,"Oversized ELF input");
        constexpr std::array<std::uint8_t,7> magic{0x7f,'E','L','F',2,1,1};
        Require(raw.size()>=64 && std::equal(magic.begin(),magic.end(),raw.begin()) &&
            Value<std::uint16_t>(18)==62 && Value<std::uint32_t>(20)==1,"Expected decrypted ELF64 little-endian x86-64 input");
        type=Value<std::uint16_t>(16); entry=Value<std::uint64_t>(24);
        Require(type==0xfe10 || type==0xfe18,"Unexpected ELF executable type");
        const auto count=Value<std::uint16_t>(56);
        Require(Value<std::uint16_t>(52)==64 && Value<std::uint16_t>(54)==56 && count && count<=64,
            "Invalid ELF/program header sizes");
        const auto table=Value<std::uint64_t>(32);
        (void)Slice(table,std::uint64_t{count}*56);
        std::vector<Header> headers;
        for (std::uint64_t i=0;i<count;++i) {
            const auto p=table+i*56;
            Header h{Value<std::uint32_t>(p),Value<std::uint32_t>(p+4),Value<std::uint64_t>(p+8),
                Value<std::uint64_t>(p+16),Value<std::uint64_t>(p+32),Value<std::uint64_t>(p+40),Value<std::uint64_t>(p+48)};
            (void)Slice(h.offset,h.file_size); headers.push_back(h);
            if (h.kind==0x61000001 && !procparam) procparam=h.address;
            if (h.kind==7) tls_bytes|=h.memory_size;
            if(h.kind==0x6474e550){Require(!unwind_present,"Duplicate unwind header");unwind_present=true;unwind_address=h.address;unwind_size=h.file_size;}
            if (h.kind!=1 && h.kind!=0x61000010) continue;
            Require(h.memory_size && h.file_size<=h.memory_size && h.address<=kLimit && h.memory_size<=kLimit-h.address,
                "Invalid ELF load segment extent");
            Require(!(h.flags&~7u) && (!h.alignment || !(h.alignment&(h.alignment-1))),"Invalid ELF segment flags/alignment");
            segments.push_back(h); image_size=(std::max)(image_size,h.address+h.memory_size);
        }
        Require(!segments.empty() && segments.size()<=32,"Invalid ELF load segment count");
        std::stable_sort(segments.begin(),segments.end(),[](auto& a,auto& b){return a.address<b.address;});
        for (std::size_t i=1;i<segments.size();++i)
            Require(segments[i-1].address+segments[i-1].memory_size<=segments[i].address,"Overlapping ELF load segments");
        Require(!entry || Contains(entry,1,true),"ELF entry is outside executable file bytes");
        auto one=[&](std::uint32_t kind) {
            Header selected; unsigned found{};
            for (const auto& h:headers) if (h.kind==kind) {selected=h;++found;}
            Require(found==1,"Missing or duplicate required ELF program header"); return selected;
        };
        const auto d=one(2); dyn_data_=one(0x61000000);
        Require(d.file_size && d.file_size%16==0 && d.file_size/16<=1000000,"Invalid ELF dynamic table shape");
        bool terminated{};
        for (std::uint64_t i=0;i<d.file_size;i+=16) {
            const auto tag=Value<std::uint64_t>(d.offset+i),value=Value<std::uint64_t>(d.offset+i+8);
            if (!tag) {terminated=true;break;}
            dynamic.emplace_back(tag,value); tags[tag]=value;
        }
        Require(terminated,"Unterminated ELF dynamic table");
        for (auto tag:{kStrings,kStringBytes,kSymbols,kSymbolBytes}) Require(tags.contains(tag),"Missing ELF table declaration");
        strings_=DynamicSlice(tags.at(kStrings),tags.at(kStringBytes));
        libraries_=IdTable(true); modules_=IdTable(false);
        const auto sym=DynamicSlice(tags.at(kSymbols),tags.at(kSymbolBytes));
        Require(sym.size()%24==0 && sym.size()/24<=1000000 && (!tags.contains(0x6100003b) || tags.at(0x6100003b)==24),
            "Invalid ELF symbol table shape");
        const auto sym_offset=dyn_data_.offset+tags.at(kSymbols);
        std::uint64_t name_bytes{};
        for (std::uint64_t i=0;i<sym.size();i+=24) {
            const auto p=sym_offset+i; const auto info=Value<std::uint8_t>(p+4);
            symbols.push_back({String(Value<std::uint32_t>(p)),static_cast<std::uint8_t>(info&15),
                static_cast<std::uint8_t>(info>>4),Value<std::uint16_t>(p+6),Value<std::uint64_t>(p+8),Value<std::uint64_t>(p+16)});
            name_bytes+=symbols.back().name.size();
            Require(name_bytes<=64*1024*1024,"ELF symbol names exceed preparation memory limit");
        }
        for (auto [location,bytes]:{std::pair{0x6100002full,0x61000031ull},std::pair{0x61000029ull,0x6100002dull}}) {
            const auto offset=tags.contains(location)?tags.at(location):0, size=tags.contains(bytes)?tags.at(bytes):0;
            (void)DynamicSlice(offset,size);
            Require(size%24==0 && size/24<=1000000-relocations.size(),"Invalid ELF relocation table shape");
            for (std::uint64_t i=0;i<size;i+=24) {
                const auto p=dyn_data_.offset+offset+i, info=Value<std::uint64_t>(p+8), address=Value<std::uint64_t>(p);
                Require((info>>32)<symbols.size() && Contains(address,8),"ELF relocation lies outside mapped memory or symbols");
                relocations.push_back({address,info>>32,static_cast<std::uint32_t>(info),Value<std::int64_t>(p+16)});
            }
        }
    }
    std::map<std::string,std::string> IdTable(bool libraries) const {
        std::map<std::string,std::string> result;
        for (auto [tag,value]:dynamic) if (libraries?(tag==0x61000013 || tag==0x61000015):(tag==0x6100000d || tag==0x6100000f))
            result[EncodeId(value>>48)]=String(value&0xffffffff);
        return result;
    }
    Identity Identify(std::size_t index,bool exporting) const {
        const auto& name=symbols.at(index).name;
        const auto first=name.find('#'),last=first==name.npos?name.npos:name.find('#',first+1);
        const auto& libraries=libraries_; const auto& modules=modules_;
        const auto lib=first==name.npos?std::string{}:name.substr(first+1,(last==name.npos?name.size():last)-first-1);
        const auto mod=last==name.npos?std::string{}:name.substr(last+1);
        Require(!exporting || (first!=name.npos && last!=name.npos && name.find('#',last+1)==name.npos &&
            libraries.contains(lib) && modules.contains(mod)),"ELF export needs resolved library/module identities");
        return {name,name.substr(0,first),libraries.contains(lib)?libraries.at(lib):lib,
            modules.contains(mod)?modules.at(mod):mod,index};
    }
};
class Writer {
public:
    std::vector<std::uint8_t> bytes;
    void Raw(std::span<const std::uint8_t> value) {
        Require(value.size()<=kLimit-bytes.size(),"Prepared ELF exceeds runtime input limit");
        bytes.insert(bytes.end(),value.begin(),value.end());
    }
    template<typename T> void Value(T value) {
        using U=std::make_unsigned_t<T>; const auto bits=std::bit_cast<U>(value);
        std::array<std::uint8_t,sizeof(T)> data{};
        for (std::size_t i=0;i<data.size();++i) data[i]=static_cast<std::uint8_t>(bits>>(8*i));
        Raw(data);
    }
    void String(std::string_view value) {
        Value(static_cast<std::uint32_t>(value.size()));
        Raw({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});
    }
    void Name(const Identity& name) {
        String(name.name);String(name.friendly);String(name.library);String(name.module);
    }
};
void VerifyLibc(std::span<const std::uint8_t> source) {
    const auto plain=PlainSelfElf(source);
    const Elf elf(plain); const Symbol* selected{};
    for (const auto& symbol:elf.symbols) if (symbol.section && symbol.type==2 && symbol.name.substr(0,symbol.name.find('#'))=="bzQExy189ZI") {
        Require(!selected,"libc _init_env is not unique"); selected=&symbol;
    }
    Require(selected && selected->size==1 && elf.Contains(selected->value,1,true),"libc _init_env RET contract is not supported");
    for (const auto& segment:elf.segments) if ((segment.flags&1) && selected->value>=segment.address &&
            selected->value-segment.address<segment.file_size) {
        Require(elf.Slice(segment.offset+selected->value-segment.address,1)[0]==0xc3,"libc _init_env has behavior other than RET"); return;
    }
    throw std::runtime_error("libc _init_env is outside executable file bytes");
}
}

PreparedElf PrepareElf(std::span<const std::uint8_t> source,bool module,std::span<const std::uint8_t> libc) {
    // Decode the executable view while retaining the original file identity.
    // Module loading compares this digest with the selected content file.
    auto plain=PlainSelfElf(source);
    const Elf elf(plain);
    Require(module?(elf.type==0xfe18 && !elf.entry && !elf.procparam):(elf.type==0xfe10 && elf.entry),"ELF does not match requested executable/module contract");
    Require(!elf.tls_bytes,"Static TLS is not supported by the prepared-image runtime");
    Require(elf.procparam<elf.image_size,"ELF process parameters lie outside image extent");
    if (!libc.empty()) VerifyLibc(libc);
    auto tag=[&](std::uint64_t id){return elf.tags.contains(id)?elf.tags.at(id):0;};
    const std::uint64_t init=tag(12),fini=tag(13),flags=(elf.tags.contains(12)?1ull:0)|(elf.tags.contains(13)?2ull:0);
    if (module) {
        Require(!tag(27) && !tag(28) && (flags&1),"Module init/fini arrays or missing DT_INIT require a different startup contract");
        Require(elf.Contains(init,1,true) && (!(flags&2) || elf.Contains(fini,1,true)),"Module init/fini lies outside executable file bytes");
    }
    std::set<std::uint64_t> referenced;
    for (const auto& item:elf.relocations) referenced.insert(item.symbol);
    std::vector<Identity> imports,exports;
    std::map<std::size_t,std::uint64_t> import_indices;
    std::set<std::tuple<std::string,std::string,std::string>> identities;
    for (std::size_t i=0;i<elf.symbols.size();++i) {
        const auto& symbol=elf.symbols[i];
        if (symbol.name.empty()) continue;
        if (!symbol.section) {
            if (symbol.type==0 && symbol.binding==2 && !referenced.contains(i)) continue;
            Require(symbol.type==1 || symbol.type==2,"Unsupported import symbol kind");
            Require(imports.size()<10000,"Prepared ELF import limit exceeded");
            import_indices[i]=imports.size(); imports.push_back(elf.Identify(i,false));
        } else if (module && (symbol.binding==1 || symbol.binding==2)) {
            Require(exports.size()<10000,"Prepared ELF export limit exceeded");
            Require(symbol.section!=0xfff1 && (symbol.type==1 || symbol.type==2),"Unsupported exported symbol kind");
            Require(elf.Contains(symbol.value,(std::max)(1ull,symbol.size),symbol.type==2),"Export lies outside its mapped segment");
            auto identity=elf.Identify(i,true);
            Require(identities.emplace(identity.friendly,identity.library,identity.module).second,"Duplicate ELF export identity");
            exports.push_back(std::move(identity));
        }
    }
    Require(imports.size()<=10000 && exports.size()<=10000,"Prepared ELF import/export limit exceeded");
    Writer writer;
    const std::string_view magic=module?"KTMODULE":"KTPROBE1";
    writer.Raw({reinterpret_cast<const std::uint8_t*>(magic.data()),magic.size()});
    for (std::uint64_t value:{elf.image_size,elf.entry,static_cast<std::uint64_t>(elf.segments.size()),
            static_cast<std::uint64_t>(elf.relocations.size()),static_cast<std::uint64_t>(imports.size()),elf.procparam,std::uint64_t(!libc.empty())|2ull}) writer.Value(value);
    const auto sha=Hash(source); writer.Raw(sha);
    if (module && EspradeLatencyModule(sha)) {
        bool applied{};
        for (const auto& segment:elf.segments) if (segment.flags&1) {
            applied|=ApplyEspradeLatency(std::span<std::uint8_t>(plain).subspan(
                static_cast<std::size_t>(segment.offset),static_cast<std::size_t>(segment.file_size)),segment.address,sha);
        }
        Require(applied,"ESP Ra.De. low-latency selector is outside executable bytes");
    }
    if (module) for (std::uint64_t value:{init,fini,flags,static_cast<std::uint64_t>(exports.size())}) writer.Value(value);
    for (const auto& segment:elf.segments) {
        for (std::uint64_t value:{segment.address,segment.memory_size,std::uint64_t{segment.flags},segment.file_size}) writer.Value(value);
        writer.Raw(elf.Slice(segment.offset,segment.file_size));
    }
    for (const auto& identity:imports) { writer.Name(identity); writer.Value(std::uint32_t{elf.symbols[identity.index].type}); }
    for (const auto& identity:exports) {
        const auto& symbol=elf.symbols[identity.index]; writer.Name(identity);
        writer.Value(std::uint32_t{symbol.type}); writer.Value(std::uint32_t{symbol.binding});
        writer.Value(symbol.value); writer.Value(symbol.size);
    }
    for (const auto& relocation:elf.relocations) {
        std::uint64_t kind{},value{};
        if (relocation.type==8) Require(!relocation.symbol,"RELATIVE relocation names a symbol");
        else {
            Require(relocation.type==1 || relocation.type==6 || relocation.type==7,"Unsupported ELF relocation type");
            const auto& symbol=elf.symbols[static_cast<std::size_t>(relocation.symbol)];
            if (!symbol.section) {
                Require(import_indices.contains(static_cast<std::size_t>(relocation.symbol)),"Relocation references an empty undefined symbol");
                kind=1; value=import_indices.at(static_cast<std::size_t>(relocation.symbol));
            } else if (symbol.section==0xfff1) {kind=2;value=symbol.value;}
            else { Require(elf.Contains(symbol.value,(std::max)(1ull,symbol.size)),"Defined relocation symbol lies outside mapped segments"); value=symbol.value; }
        }
        writer.Value(relocation.address);writer.Value(kind);writer.Value(value);writer.Value(relocation.addend);
    }
    writer.Value(elf.unwind_address);writer.Value(elf.unwind_size);
    return {std::move(writer.bytes),sha,elf.image_size,imports.size(),exports.size(),elf.relocations.size(),elf.segments.size()};
}
}
