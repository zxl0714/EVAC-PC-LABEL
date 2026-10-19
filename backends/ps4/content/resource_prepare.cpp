// Native counterpart of scripts/ketsui/game_fixes.py. PSB/MDF format reference:
// FreeMote d87be1f3b8458c81822825fd6d56123d4df860b4 (no resource bodies embedded).
#include "resource_prepare.h"
#include "common/paths.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <zlib.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace cave::ps4 {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kResourceLimit = 32 * 1024 * 1024;
constexpr std::size_t kPatchLimit = 262144;
constexpr std::string_view kExecutableHash = "a32b4b798cc09d8b3f756acde33d679dbb7fec1a54eec50cc179f3f3694ac073";
constexpr std::string_view kDaioujouExecutableHash = "365b5db9be3808ecfffe8e9ddd28ac1b740a0326cd0d16fb0c63af22a76a3c2f";
void Require(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
std::string Hex(std::span<const std::byte> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value; value.reserve(bytes.size()*2);
    for (auto b:bytes) { auto c=std::to_integer<unsigned>(b); value+=digits[c>>4]; value+=digits[c&15]; }
    return value;
}
std::span<const std::byte> ByteSpan(std::span<const std::uint8_t> bytes) { return std::as_bytes(bytes); }
std::string Hash(std::span<const std::uint8_t> bytes) { return Hex(kt::content::ReadPatchHash(ByteSpan(bytes))); }
std::string Text(std::span<const std::uint8_t> bytes) { return {reinterpret_cast<const char*>(bytes.data()),bytes.size()}; }
ResourceBytes Bytes(std::string_view value) { return {value.begin(),value.end()}; }
std::uint64_t Number(std::span<const std::uint8_t> bytes, std::uint64_t at, unsigned size) {
    Require(size && size<=8 && at<=bytes.size() && size<=bytes.size()-at,"Truncated resource integer");
    std::uint64_t value{};
    for (unsigned i=0;i<size;++i) value|=std::uint64_t{bytes[static_cast<std::size_t>(at)+i]}<<(i*8);
    return value;
}
void Append(ResourceBytes& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i=0;i<width;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
void Append(ResourceBytes& bytes, std::span<const std::byte> value) {
    for (auto b:value) bytes.push_back(std::to_integer<std::uint8_t>(b));
}
void Append(ResourceBytes& bytes, std::string_view value) { bytes.insert(bytes.end(),value.begin(),value.end()); }

// Integer-seeded MT19937 initialization used by Python's random.Random, not
// std::mt19937(uint32_t) or std::seed_seq. The four little-endian MD5 words
// initialize the full state; the zero high words of an integer are omitted.
class IntegerRandom {
    std::array<std::uint32_t,624> state_{};
    std::size_t at_=624;
public:
    explicit IntegerRandom(std::span<const std::uint8_t> digest) {
        std::vector<std::uint32_t> key;
        for (std::size_t i=0;i<digest.size();i+=4) key.push_back(static_cast<std::uint32_t>(Number(digest,i,4)));
        while (key.size()>1 && !key.back()) key.pop_back();
        Require(!key.empty(),"Empty archive mask seed");
        state_[0]=19650218;
        for (std::size_t i=1;i<state_.size();++i)
            state_[i]=1812433253u*(state_[i-1]^(state_[i-1]>>30))+static_cast<std::uint32_t>(i);
        std::size_t i=1,j=0;
        for (std::size_t k=0;k<(std::max)(state_.size(),key.size());++k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1664525u))+key[j]+static_cast<std::uint32_t>(j);
            if (++i==state_.size()) {state_[0]=state_.back();i=1;}
            if (++j==key.size()) j=0;
        }
        for (std::size_t k=1;k<state_.size();++k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1566083941u))-static_cast<std::uint32_t>(i);
            if (++i==state_.size()) {state_[0]=state_.back();i=1;}
        }
        state_[0]=0x80000000;
    }
    std::uint32_t Next() {
        if (at_==state_.size()) {
            for (std::size_t i=0;i<state_.size();++i) {
                const auto joined=(state_[i]&0x80000000u)|(state_[(i+1)%624]&0x7fffffffu);
                state_[i]=state_[(i+397)%624]^(joined>>1)^((joined&1)?0x9908b0dfu:0u);
            }
            at_=0;
        }
        auto value=state_[at_++];
        value^=value>>11; value^=(value<<7)&0x9d2c5680u; value^=(value<<15)&0xefc60000u; value^=value>>18;
        return value;
    }
};

class Psb {
    std::span<const std::uint8_t> raw_;
    std::array<std::uint64_t,8> header_{};
    std::vector<std::string> names_;
    std::vector<std::uint64_t> strings_;
    std::uint64_t nodes_{},text_bytes_{};
    std::pair<std::vector<std::uint64_t>,std::uint64_t> Array(std::uint64_t at) const {
        const auto marker=Number(raw_,at,1);
        Require(marker>=13 && marker<=20,"Invalid PSB array count width");
        const auto width=static_cast<unsigned>(marker-12);
        const auto count=Number(raw_,at+1,width);
        at+=1+width;
        const auto item=Number(raw_,at,1);
        Require(item>=13 && item<=20,"Invalid PSB array item width");
        ++at;
        const auto item_width=static_cast<unsigned>(item-12);
        Require(count<=1000000 && at<=raw_.size() && count<=(raw_.size()-at)/item_width,"Invalid PSB array extent");
        std::vector<std::uint64_t> values; values.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i=0;i<count;++i) values.push_back(Number(raw_,at+i*item_width,item_width));
        return {std::move(values),at+count*item_width};
    }
    std::string CheckedText(std::string value) {
        text_bytes_+=value.size();
        Require(text_bytes_<=64*1024*1024 && ValidUtf8(value),"Invalid or oversized PSB UTF-8 strings");
        return value;
    }
    std::uint64_t Relative(std::uint64_t base,std::uint64_t offset) const {
        Require(base<=raw_.size() && offset<raw_.size()-base,"PSB offset is out of bounds"); return base+offset;
    }
    Json Value(std::uint64_t at,unsigned depth) {
        Require(depth<=64 && ++nodes_<=1000000,"PSB recursion bound");
        const auto kind=Number(raw_,at,1); ++at;
        if (kind<=1) return nullptr;
        if (kind<=3) return kind==3;
        if (kind==4) return 0;
        if (kind<=12) {
            const auto width=static_cast<unsigned>(kind-4); auto n=Number(raw_,at,width);
            if (width<8 && (n&(std::uint64_t{1}<<(width*8-1)))) n|=(~std::uint64_t{})<<(width*8);
            return std::bit_cast<std::int64_t>(n);
        }
        if (kind<=20) return Array(at-1).first;
        if (kind<=24) {
            const auto index=Number(raw_,at,static_cast<unsigned>(kind-20));
            Require(index<strings_.size(),"PSB string index out of range");
            const auto start=Relative(header_[3],strings_[static_cast<std::size_t>(index)]);
            const auto rest=raw_.subspan(static_cast<std::size_t>(start));
            const auto end=std::find(rest.begin(),rest.end(),0);
            Require(end!=rest.end(),"Unterminated PSB string");
            return CheckedText(Text(rest.first(static_cast<std::size_t>(end-rest.begin()))));
        }
        if (kind<=28) return Json{{"resource_index",Number(raw_,at,static_cast<unsigned>(kind-24))}};
        if (kind==29) return 0.0;
        if (kind<=31) {
            const double result=kind==30 ? std::bit_cast<float>(static_cast<std::uint32_t>(Number(raw_,at,4))) :
                std::bit_cast<double>(Number(raw_,at,8));
            Require(std::isfinite(result),"Nonfinite PSB number"); return result;
        }
        if (kind==32) {
            const auto [offsets,base]=Array(at); auto values=Json::array();
            for (auto offset:offsets) values.push_back(Value(Relative(base,offset),depth+1));
            return values;
        }
        if (kind==33) {
            const auto [names,after_names]=Array(at); const auto [offsets,base]=Array(after_names);
            Require(names.size()==offsets.size(),"Invalid PSB object key extent");
            auto values=Json::object();
            for (std::size_t i=0;i<names.size();++i) {
                Require(names[i]<names_.size(),"PSB object name out of range");
                const auto& name=names_[static_cast<std::size_t>(names[i])];
                Require(!values.contains(name),"Duplicate PSB object key");
                values[name]=Value(Relative(base,offsets[i]),depth+1);
            }
            return values;
        }
        throw std::runtime_error("Unsupported PSB value");
    }
public:
    explicit Psb(std::span<const std::uint8_t> raw):raw_(raw) {
        Require(raw.size()>=40 && raw.size()<=kResourceLimit && Text(raw.first(8))==std::string("PSB\0\2\0\0\0",8),"Expected a PSB v2 index");
        for (unsigned i=0;i<8;++i) header_[i]=Number(raw,8+i*4,4);
        auto [charset,at]=Array(header_[1]); auto [parents,next]=Array(at); auto [terminals,unused]=Array(next);
        (void)unused;
        for (auto terminal:terminals) {
            Require(terminal<parents.size(),"PSB name terminal out of range");
            std::string name;
            for (auto node=parents[static_cast<std::size_t>(terminal)];node;) {
                Require(node<parents.size() && name.size()<=4096,"Invalid PSB name chain");
                const auto parent=parents[static_cast<std::size_t>(node)];
                Require(parent<charset.size() && node>=charset[static_cast<std::size_t>(parent)] &&
                    node-charset[static_cast<std::size_t>(parent)]<=255,"Invalid PSB name character");
                name+=static_cast<char>(node-charset[static_cast<std::size_t>(parent)]); node=parent;
            }
            std::reverse(name.begin(),name.end()); names_.push_back(CheckedText(std::move(name)));
        }
        strings_=Array(header_[2]).first;
    }
    Json Root() { return Value(header_[7],0); }
};

std::size_t Count(std::string_view text,std::string_view anchor) {
    std::size_t count{},at{};
    while ((at=text.find(anchor,at))!=text.npos) {++count;at+=anchor.size();} return count;
}
void Replace(std::string& text,std::string_view anchor,std::string_view replacement,std::size_t count) {
    Require(Count(text,anchor)==count,"Resource script differs from the verified control flow");
    std::size_t at{};
    while ((at=text.find(anchor,at))!=text.npos) {text.replace(at,anchor.size(),replacement);at+=replacement.size();}
}
std::string Compact(std::string_view text) {
    // Match bytes.splitlines(keepends=True): CR, LF and CRLF are separators.
    std::string result;
    for (std::size_t at=0;at<text.size();) {
        const auto end=text.find_first_of("\r\n",at);
        auto next=end==text.npos ? text.size() : end+1;
        if (end!=text.npos && text[end]=='\r' && next<text.size() && text[next]=='\n') ++next;
        const auto line=text.substr(at,next-at);
        const auto first=line.find_first_not_of(" \t\r\n\v\f");
        if (first==line.npos || !line.substr(first).starts_with("//")) result+=line;
        at=next;
    }
    return result;
}
std::tuple<std::size_t,std::size_t,std::string> Method(const std::string& raw,std::string_view begin,std::string_view end) {
    Require(Count(raw,begin)==1 && Count(raw,end)==1,"Expected one script method boundary");
    const auto start=raw.find(begin),stop=raw.find(end);
    Require(stop>start,"Invalid script method boundary order"); return {start,stop,raw.substr(start,stop-start)};
}
ResourceBytes ReadFile(const fs::path& path,std::size_t limit) {
    Require(fs::is_regular_file(path) && fs::file_size(path)<=limit,"Missing or oversized resource preparation input");
    ResourceBytes bytes(static_cast<std::size_t>(fs::file_size(path)));
    std::ifstream source(path,std::ios::binary);
    Require(bool(source.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) &&
        source.peek()==std::char_traits<char>::eof(),"Resource preparation input changed while reading");
    return bytes;
}
ResourceBytes ReadRange(const fs::path& path,std::uint64_t file_size,std::uint64_t offset,std::uint64_t size) {
    Require(fs::file_size(path)==file_size && offset<=file_size && size && size<=kPatchLimit && size<=file_size-offset,
        "Resource script exceeds original archive");
    std::ifstream source(path,std::ios::binary); source.seekg(static_cast<std::streamoff>(offset));
    ResourceBytes bytes(static_cast<std::size_t>(size));
    Require(bool(source.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size))),"Truncated resource archive read"); return bytes;
}
ResourceBytes ArchiveKey(std::span<const std::uint8_t> executable,bool daioujou=false) {
    Require(Hash(executable)==(daioujou?kDaioujouExecutableHash:kExecutableHash),
        daioujou?"Game fixes require the verified CUSA32997 1.04 executable":"Game fixes require the verified CUSA09062 1.02 executable");
    const auto count=Number(executable,56,2),table=Number(executable,32,8);
    Require(count<=64 && table<=executable.size() && count<=(executable.size()-table)/56,"Invalid archive executable headers");
    auto virtual_read=[&](std::uint64_t address,unsigned size) {
        ResourceBytes bytes; unsigned matches{};
        for (std::uint64_t i=0;i<count;++i) {
            const auto at=table+i*56,kind=Number(executable,at,4);
            if (kind!=1 && kind!=0x61000010) continue;
            const auto base=Number(executable,at+16,8),extent=Number(executable,at+32,8),offset=Number(executable,at+8,8);
            if (address<base || size>extent || address-base>extent-size) continue;
            const auto delta=address-base;
            Require(offset<=executable.size() && delta<=executable.size()-offset && size<=executable.size()-offset-delta,"Invalid archive seed extent");
            const auto data=executable.subspan(static_cast<std::size_t>(offset+delta),size);
            bytes.assign(data.begin(),data.end()); ++matches;
        }
        Require(matches==1,"Archive initialization data is not in one file-backed segment"); return bytes;
    };
    if (daioujou) {
        Require(virtual_read(0xdd,3)==ResourceBytes{0x48,0x8d,0x35} &&
            virtual_read(0xe4,5)==ResourceBytes{0xba,0x0d,0,0,0},"Archive initialization instruction layout changed");
        const auto relative=virtual_read(0xe0,4);
        const auto address=std::int64_t{0xe4}+static_cast<std::int32_t>(Number(relative,0,4));
        Require(address>=0,"Invalid archive initialization address");
        return virtual_read(static_cast<std::uint64_t>(address),13);
    }
    Require(virtual_read(0xd3,2)==ResourceBytes{0x48,0xb8} && virtual_read(0xdd,2)==ResourceBytes{0x48,0xb9},
        "Archive initialization instruction layout changed");
    auto key=virtual_read(0xdf,8); const auto tail=virtual_read(0xd8,5); key.insert(key.end(),tail.begin(),tail.end()); return key;
}
}

ResourceBytes ArchiveMask(std::span<const std::uint8_t> seed,std::string_view name) {
    Require(seed.size()<=4096 && name.size()<=4096 && ValidUtf8(name),"Invalid archive seed or name");
    ResourceBytes input(seed.begin(),seed.end()); Append(input,name);
    BCRYPT_ALG_HANDLE algorithm{};
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_MD5_ALGORITHM,nullptr,0)>=0,"Cannot open archive MD5");
    std::array<std::uint8_t,16> digest{};
    const auto result=BCryptHash(algorithm,nullptr,0,input.data(),static_cast<ULONG>(input.size()),digest.data(),static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0); Require(result>=0,"Cannot calculate archive mask");
    IntegerRandom random(digest); ResourceBytes mask;
    for (unsigned i=0;i<16;++i) Append(mask,random.Next(),4);
    return mask;
}
ResourceBytes UnwrapResource(std::span<const std::uint8_t> source,std::span<const std::uint8_t> seed,std::string_view name) {
    Require(source.size()<=kResourceLimit,"Oversized archive resource");
    if (source.size()<4 || Text(source.first(4))!=std::string("mdf\0",4)) return {source.begin(),source.end()};
    Require(source.size()>=8,"Truncated MDF header");
    const auto expected=Number(source,4,4); Require(expected<=kResourceLimit,"Oversized MDF output");
    const auto mask=ArchiveMask(seed,name); ResourceBytes encoded(source.begin()+8,source.end());
    for (std::size_t i=0;i<encoded.size();++i) encoded[i]^=mask[i%64];
    ResourceBytes output(static_cast<std::size_t>(expected)+1);
    z_stream stream{};
    Require(inflateInit(&stream)==Z_OK,"Cannot initialize MDF inflater");
    stream.next_in=encoded.data(); stream.avail_in=static_cast<uInt>(encoded.size());
    stream.next_out=output.data(); stream.avail_out=static_cast<uInt>(output.size());
    const auto status=inflate(&stream,Z_FINISH); const auto consumed=stream.total_in,produced=stream.total_out;
    inflateEnd(&stream);
    Require(status==Z_STREAM_END && produced==expected && consumed<=encoded.size() &&
        std::all_of(encoded.begin()+static_cast<std::ptrdiff_t>(consumed),encoded.end(),[](auto b){return b==0;}),
        "Invalid MDF output extent or trailing data");
    output.resize(static_cast<std::size_t>(expected)); return output;
}
ResourceBytes WrapResource(std::span<const std::uint8_t> source,std::span<const std::uint8_t> seed,std::string_view name,std::size_t extent) {
    Require(source.size()<=kResourceLimit && extent>=8 && extent<=kResourceLimit,"Invalid fixed resource extent");
    ResourceBytes payload(compressBound(static_cast<uLong>(source.size())));
    auto size=static_cast<uLongf>(payload.size());
    Require(compress2(payload.data(),&size,source.data(),static_cast<uLong>(source.size()),9)==Z_OK && size<=extent-8,
        "Fixed-size resource overlay does not fit the original extent");
    payload.resize(static_cast<std::size_t>(size)); payload.resize(extent-8); const auto mask=ArchiveMask(seed,name);
    ResourceBytes output{'m','d','f',0}; Append(output,source.size(),4);
    for (std::size_t i=0;i<payload.size();++i) output.push_back(payload[i]^mask[i%64]);
    Require(UnwrapResource(output,seed,name)==ResourceBytes(source.begin(),source.end()),"Prepared resource failed its decode roundtrip"); return output;
}
Json ParseResourceIndex(std::span<const std::uint8_t> source) { return Psb(source).Root(); }
ResourceBytes FixResourceScript(std::span<const std::uint8_t> source,ResourceFix fix) {
    Require(source.size()<=kResourceLimit,"Oversized resource script");
    auto raw=Text(source); const std::string newline=raw.find("\r\n")!=raw.npos ? "\r\n" : "\n";
    if (fix==ResourceFix::CommunityReplayPeer) {
        auto [start,stop,body]=Method(raw,"function is_blocked_id(_id_h,_id_l) {","function getLocalEntryMax() {");
        // Only the community-loaded title adapter uses this fallback. Keep the
        // working Sony ID-mapper/list path intact; do not enable socket APIs or
        // alter privacy settings. A disabled/uninitialized provider fails closed.
        Replace(body,"  if (_net.initializeIDMapper() < 0) return true;",
            "  if (_net.initializeIDMapper() < 0) {"+newline+
            "    if (::g_ranking_manager && ::g_ranking_manager.isInitialized()) return false;"+newline+
            "    return true;"+newline+"  }",1);
        raw.replace(start,stop-start,Compact(body));return Bytes(raw);
    }
    if (fix==ResourceFix::MusicAutosave) {
        constexpr std::string_view anchor="m_bgm_mode[_gamemode] = _bgmMode;";
        Replace(raw,anchor,std::string(anchor)+newline+
            "        ::g_systemdata.get_value(SystemDataValueIndex.SETTING_SOUND).set_bgm_set_mode(_gamemode, _bgmMode);"+newline+
            "        ::g_systemdata.CheckValueChangeAutosave(SystemDataValueIndex.SETTING_SOUND);"+newline+
            "        this._check_autosave();",2);
        return Bytes(Compact(raw));
    }
    if (fix==ResourceFix::ReplayBatch) {
        auto [start,stop,body]=Method(raw,"function RankingManager::_recordEntryBody(","function friend_ranking_sort_func(");
        constexpr std::string_view score="  local result = this._recordScore(_boardIdArray, _entryInfo);";
        Replace(body,score,std::string(score)+newline+"  local first_transfer_error = null;",1);
        Replace(body,"          if ( !this.isSucceeded(result) ) "+newline+"            break;",
            "          if ( !this.isSucceeded(result) && first_transfer_error == null )"+newline+"            first_transfer_error = result;",1);
        Replace(body,"  return result;","  return first_transfer_error != null ? first_transfer_error : result;",1);
        raw.replace(start,stop-start,body); return Bytes(Compact(raw));
    }
    Require(fix==ResourceFix::LocalRoundGate,"Unknown resource fix");
    auto [start,stop,body]=Method(raw,"  function checkRankingAndRegistRanking() {","  function checkRegistRanking() {");
    Require(Count(body,"if (checkRegistRanking()) {")==1 &&
        Count(body,"local _ret = ::g_ranking_ctrl.registRankingAttackHighscore(m_replay_score, _ship);")==2,
        "Post-game ranking submission differs from verified control flow");
    Replace(body,"      if (ranking_is_network_ok_with_sign_in() && !_net.getUgcRestricted()) {",
        "      if (::g_ranking_manager.isInitialized()) {",1);
    for (const auto mode:{"GameMode.ETC + _round_type","_gamemode"}) {
        const auto initializer="initRanking(this, true, this.TYPE_MODE_ARRAY_DEFAULT["+std::string(mode)+"]";
        Replace(body,initializer+");",initializer+", LEADERBOARD_PLAYER_FILTER_MINE, true);",1);
    }
    raw.replace(start,stop-start,Compact(body)); return Bytes(raw);
}
ResourceBytes EncodeReadFixes(const kt::content::ReadPatchSet& patches) {
    const auto digits=[](std::string_view s){return std::ranges::all_of(s,[](char c){return c>='0' && c<='9';});};
    Require(patches.title.size()==9 && patches.title.starts_with("CUSA") && digits(std::string_view(patches.title).substr(4)) &&
        patches.version.size()==5 && patches.version[2]=='.' && digits(std::string_view(patches.version).substr(0,2)) &&
        digits(std::string_view(patches.version).substr(3)),"Invalid read-overlay application identity");
    Require(!patches.entries.empty() && patches.entries.size()<=16,"Invalid read-overlay count");
    auto entries=patches.entries;
    std::ranges::sort(entries,[](const auto& a,const auto& b){return a.path!=b.path ? a.path<b.path : a.offset<b.offset;});
    ResourceBytes result{'K','T','F','I','X','0','1',0};
    Append(result,entries.size(),4); Append(result,9,4); Append(result,5,4); Append(result,0,4);
    Append(result,patches.title); Append(result,patches.version);
    const kt::content::ReadPatch* previous=nullptr;
    for (const auto& patch:entries) {
        const auto& name=patch.path;
        Require(!name.empty() && name.size()<=256 && ValidUtf8(name) && !name.starts_with('/') && !name.ends_with('/') &&
            std::ranges::none_of(name,[](unsigned char c){return c<32 || c==':' || c=='\\';}),"Invalid read-overlay relative path");
        for (std::size_t at=0;at<name.size();) {
            const auto slash=name.find('/',at); const auto part=std::string_view(name).substr(at,slash==name.npos ? slash : slash-at);
            Require(!part.empty() && part!="." && part!="..","Invalid read-overlay relative component");
            if (slash==name.npos) break; at=slash+1;
        }
        Require(patch.file_size<=INT64_MAX && patch.offset<=patch.file_size && !patch.replacement.empty() &&
            patch.replacement.size()<=kPatchLimit && patch.replacement.size()<=patch.file_size-patch.offset,"Read-overlay range exceeds original file");
        Require(!previous || previous->path!=name || (previous->file_size==patch.file_size &&
            previous->offset+previous->replacement.size()<=patch.offset),"Overlapping or inconsistent read overlays");
        Append(result,name.size(),4); Append(result,patch.replacement.size(),4); Append(result,patch.file_size,8); Append(result,patch.offset,8);
        Append(result,patch.original_sha256); Append(result,kt::content::ReadPatchHash(patch.replacement));
        Append(result,name); Append(result,patch.replacement); previous=&patch;
    }
    Require(result.size()<=4*1024*1024,"Oversized read-overlay file"); return result;
}
PreparedReadFixes PrepareReadFixes(const fs::path& data,bool community_replay,bool community_offline) {
    community_replay|=community_offline;
    const auto root=fs::canonical(data);
    const auto executable=DataChild(root,"CUSA09062-patch/eboot.bin");
    const auto raw_executable=ReadFile(executable,512*1024*1024),key=ArchiveKey(raw_executable);
    std::vector<std::pair<fs::path,std::string>> indices;
    auto index=[&](std::string_view path,std::string_view name) {
        const auto file=DataChild(root,path); const auto bytes=ReadFile(file,kResourceLimit);
        indices.emplace_back(file,Hash(bytes)); return ParseResourceIndex(UnwrapResource(bytes,key,name));
    };
    const auto base=index("CUSA09062-app/ps4data_info.psb.m","ps4data_info.psb.m");
    kt::content::ReadPatchSet patches{"CUSA09062","01.02",{}};
    Json report{{"format",1},{"fix","direct-menu-music-autosave"},{"title_id","CUSA09062"},{"app_version","01.02"},
        {"executable_sha256",kExecutableHash},{"original_files_modified",false}};
    auto fix=[&](const Json& file_index,std::string_view resource,std::string_view archive,std::string_view path,
                 std::string_view expected,ResourceFix transform) {
        const auto& extent=file_index.at("file_info").at(std::string(resource));
        Require(extent.is_array() && extent.size()==2 && extent[0].is_number_integer() && !extent[0].is_boolean() &&
            extent[1].is_number_integer() && !extent[1].is_boolean(),"Invalid resource index entry");
        for (const auto& n:extent) Require(n.is_number_unsigned() ? n.get<std::uint64_t>()<=INT64_MAX : n.get<std::int64_t>()>=0,"Invalid resource index integer");
        const auto offset=extent[0].get<std::uint64_t>(),size=extent[1].get<std::uint64_t>();
        const auto body=DataChild(root,archive); const auto file_size=fs::file_size(body);
        const auto packed=ReadRange(body,file_size,offset,size);
        const auto name=resource.substr(resource.rfind('/')+1); const auto script=UnwrapResource(packed,key,name);
        Require(Hash(script)==expected,"Resource script differs from the verified input");
        const auto fixed=FixResourceScript(script,transform),replacement=WrapResource(fixed,key,name,static_cast<std::size_t>(size));
        kt::content::ReadPatch patch{std::string(path),file_size,offset,kt::content::ReadPatchHash(ByteSpan(packed)),{}};
        const auto bytes=ByteSpan(replacement); patch.replacement.assign(bytes.begin(),bytes.end());
        patches.entries.push_back(std::move(patch));
        return Json{{"resource",resource},{"offset",offset},{"bytes",size},{"original_script_sha256",expected},{"fixed_script_sha256",Hash(fixed)}};
    };
    const auto music=fix(base,"5000/script/title_mode_direct_menu.nut.m","CUSA09062-app/ps4data_body.bin","ps4data_body.bin",
        "83ba95c0ff110a17f9e641addc59e61b89c05431e57a91235eb15c872045f672",ResourceFix::MusicAutosave);
    report.update(music); report["original_file_bytes"]=patches.entries.front().file_size;
    report["original_packed_sha256"]=Hex(patches.entries.front().original_sha256);
    if (community_replay) report["community_replay_batch"]=fix(base,"system/script/ranking_manager.nut.m",
        "CUSA09062-app/ps4data_body.bin","ps4data_body.bin","8826a4a6545d302df876f9096a15147a036098d724d2fbd6c7f6a2349f972e43",ResourceFix::ReplayBatch);
    if (community_offline) {
        const auto patch=index("CUSA09062-patch/ps4data_patch_info.psb.m","ps4data_patch_info.psb.m");
        auto offline=fix(patch,"5000/script/title_standalone.nut.m","CUSA09062-patch/ps4data_patch_body.bin","ps4data_patch_body.bin",
            "889ac1152f99d774125e199179264887aa8a21986f670e5c48fc79d0c099db9d",ResourceFix::LocalRoundGate);
        offline.update(Json{{"layer","patch"},{"scope","eligible-post-game-submission"},{"requires_native_rounds",true}});
        report["community_local_rounds"]=std::move(offline);
    }
    auto output=EncodeReadFixes(patches);
    Require(Hash(ReadFile(executable,512*1024*1024))==kExecutableHash,"Game executable changed during fix preparation");
    for (const auto& [file,hash]:indices) Require(Hash(ReadFile(file,kResourceLimit))==hash,"Resource index changed during preparation");
    for (const auto& patch:patches.entries) {
        const auto file=DataChild(root,patch.path=="ps4data_body.bin" ? "CUSA09062-app/ps4data_body.bin" : "CUSA09062-patch/ps4data_patch_body.bin");
        Require(kt::content::ReadPatchHash(ByteSpan(ReadRange(file,patch.file_size,patch.offset,patch.replacement.size())))==patch.original_sha256,
            "Original resource changed during fix preparation");
    }
    report["read_overlay_sha256"]=Hash(output); return {std::move(output),std::move(report)};
}
PreparedReadFixes PrepareDaioujouReadFixes(const fs::path& data) {
    const auto root=fs::canonical(data),executable=DataChild(root,"eboot.bin");
    const auto original=ReadFile(executable,512*1024*1024),key=ArchiveKey(original,true);
    const auto index_path=DataChild(root,"ps4data_info.psb.m"),patch_index_path=DataChild(root,"ps4data_patch_info.psb.m");
    const auto index_raw=ReadFile(index_path,kResourceLimit),patch_index_raw=ReadFile(patch_index_path,kResourceLimit);
    const auto index=ParseResourceIndex(UnwrapResource(index_raw,key,"ps4data_info.psb.m"));
    const auto patch_index=ParseResourceIndex(UnwrapResource(patch_index_raw,key,"ps4data_patch_info.psb.m"));
    constexpr std::string_view resource="1100/script/title_ranking_view.nut.m",name="title_ranking_view.nut.m";
    Require(!patch_index.at("file_info").contains(std::string(resource)),"Unexpected patched DaiOuJou ranking script");
    const auto& extent=index.at("file_info").at(std::string(resource));
    Require(extent.is_array() && extent.size()==2,"Invalid DaiOuJou ranking resource entry");
    for (const auto& n:extent) Require(n.is_number_integer() && !n.is_boolean() &&
        (n.is_number_unsigned()?n.get<std::uint64_t>()<=INT64_MAX:n.get<std::int64_t>()>=0),"Invalid resource index integer");
    const auto offset=extent[0].get<std::uint64_t>(),size=extent[1].get<std::uint64_t>();
    const auto body=DataChild(root,"ps4data_body.bin");const auto file_size=fs::file_size(body);
    const auto packed=ReadRange(body,file_size,offset,size),script=UnwrapResource(packed,key,name);
    constexpr std::string_view expected="5275193bc6590e3d826bcdd495bc074a2f3b4adc6157c5de32d0631457e96a19";
    Require(Hash(script)==expected,"DaiOuJou ranking script differs from the verified input");
    const auto fixed=FixResourceScript(script,ResourceFix::CommunityReplayPeer);
    const auto replacement=WrapResource(fixed,key,name,static_cast<std::size_t>(size));
    kt::content::ReadPatch patch{"ps4data_body.bin",file_size,offset,kt::content::ReadPatchHash(ByteSpan(packed)),{}};
    const auto bytes=ByteSpan(replacement);patch.replacement.assign(bytes.begin(),bytes.end());
    auto output=EncodeReadFixes({"CUSA32997","01.04",{patch}});
    Require(Hash(ReadFile(executable,512*1024*1024))==kDaioujouExecutableHash &&
        Hash(ReadFile(index_path,kResourceLimit))==Hash(index_raw) &&
        Hash(ReadFile(patch_index_path,kResourceLimit))==Hash(patch_index_raw) &&
        Hash(ReadRange(body,file_size,offset,size))==Hash(packed),"DaiOuJou resources changed during preparation");
    Json report{{"format",1},{"fix","community-replay-peer-fallback"},{"title_id","CUSA32997"},{"app_version","01.04"},
        {"executable_sha256",kDaioujouExecutableHash},{"resource",resource},{"offset",offset},{"bytes",size},
        {"original_script_sha256",expected},{"fixed_script_sha256",Hash(fixed)},
        {"original_files_modified",false},{"requires_community_provider",true},{"read_overlay_sha256",Hash(output)}};
    return {std::move(output),std::move(report)};
}
}
