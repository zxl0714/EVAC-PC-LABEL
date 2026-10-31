#include "function_ranges.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace cave::ps4::cpu {
namespace {
void Require(bool value){if(!value)throw std::runtime_error("Invalid or unsupported guest function metadata");}
class View {
    std::span<const std::uint8_t> image_;
public:
    explicit View(std::span<const std::uint8_t> image):image_(image){}
    void Extent(std::uint64_t address,std::uint64_t size)const {Require(address<=image_.size() && size<=image_.size()-address);}
    template<class T>T Read(std::uint64_t address)const {
        Extent(address,sizeof(T));using U=std::make_unsigned_t<T>;U result{};
        for(std::size_t i=0;i<sizeof(T);++i)result|=static_cast<U>(image_[static_cast<std::size_t>(address)+i])<<(i*8);
        return std::bit_cast<T>(result);
    }
    std::uint64_t Leb(std::uint64_t& cursor,bool sign=false)const {
        std::uint64_t value{};unsigned shift{};std::uint8_t byte{};
        do {
            Require(shift<64);byte=Read<std::uint8_t>(cursor++);
            if(shift==63)Require((byte&0x7e)==0 || (sign && (byte&0x7e)==0x7e));
            value|=std::uint64_t(byte&0x7f)<<shift;shift+=7;
        }while(byte&0x80);
        if(sign && shift<64 && (byte&0x40))value|=~0ull<<shift;
        return value;
    }
    std::uint64_t Encoded(std::uint64_t& cursor,unsigned encoding,std::uint64_t data_base=0)const {
        const auto field=cursor;std::uint64_t value{};
        switch(encoding&15) {
        case 0:value=Read<std::uint64_t>(cursor);cursor+=8;break;
        case 1:value=Leb(cursor);break;
        case 3:value=Read<std::uint32_t>(cursor);cursor+=4;break;
        case 4:value=Read<std::uint64_t>(cursor);cursor+=8;break;
        case 9:value=Leb(cursor,true);break;
        case 11:value=static_cast<std::uint64_t>(Read<std::int32_t>(cursor));cursor+=4;break;
        case 12:value=static_cast<std::uint64_t>(Read<std::int64_t>(cursor));cursor+=8;break;
        default:Require(false);
        }
        switch(encoding&0x70) {
        case 0:break;case 0x10:value+=field;break;case 0x30:value+=data_base;break;default:Require(false);
        }
        return value;
    }
    unsigned FdeEncoding(std::uint64_t address)const {
        const auto length=Read<std::uint32_t>(address);Require(length>=8 && length!=0xffffffffu);
        Extent(address+4,length);const auto end=address+4+length;
        Require(Read<std::uint32_t>(address+4)==0);
        const auto version=Read<std::uint8_t>(address+8);Require(version==1 || version==3);
        auto cursor=address+9;std::string augmentation;
        for(;;){const auto c=Read<std::uint8_t>(cursor++);if(!c)break;Require(augmentation.size()<32);augmentation+=static_cast<char>(c);}
        (void)Leb(cursor);(void)Leb(cursor,true);
        if(version==1)++cursor;else (void)Leb(cursor);
        Require(!augmentation.empty() && augmentation[0]=='z');
        const auto augmentation_size=Leb(cursor);Require(cursor<=end && augmentation_size<=end-cursor);
        const auto augmentation_end=cursor+augmentation_size;unsigned encoding{};bool found{};
        for(std::size_t i=1;i<augmentation.size();++i) {
            switch(augmentation[i]) {
            case 'R':encoding=Read<std::uint8_t>(cursor++);found=true;break;
            case 'L':++cursor;break;
            case 'P':{const auto personality_encoding=Read<std::uint8_t>(cursor++);(void)Encoded(cursor,personality_encoding);break;}
            case 'S':break;
            default:Require(false);
            }
            Require(cursor<=augmentation_end);
        }
        Require(found && !(encoding&0x80));return encoding;
    }
};
}
std::vector<CodeRange> FunctionRanges(std::span<const std::uint8_t> image,std::uint64_t header,std::uint64_t size) {
    View view(image);view.Extent(header,size);Require(size>=12 && view.Read<std::uint8_t>(header)==1);
    auto cursor=header+4;
    const auto pointer_encoding=view.Read<std::uint8_t>(header+1);
    const auto count_encoding=view.Read<std::uint8_t>(header+2);
    const auto table_encoding=view.Read<std::uint8_t>(header+3);
    Require(pointer_encoding==0x1b && count_encoding==3 && table_encoding==0x3b);
    (void)view.Encoded(cursor,pointer_encoding,header);
    const auto count=view.Encoded(cursor,count_encoding,header);
    Require(count && count<=1000000 && count<=(size-(cursor-header))/8);
    std::vector<CodeRange> result;result.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t i=0;i<count;++i) {
        const auto start=view.Encoded(cursor,table_encoding,header);
        const auto fde=view.Encoded(cursor,table_encoding,header);
        const auto length=view.Read<std::uint32_t>(fde);Require(length>=12 && length!=0xffffffffu);view.Extent(fde+4,length);
        const auto back=view.Read<std::uint32_t>(fde+4);Require(back && back<=fde+4);
        const auto encoding=view.FdeEncoding(fde+4-back);auto entry=fde+8;
        const auto actual=view.Encoded(entry,encoding,header);
        const auto extent=view.Encoded(entry,encoding&15,header);
        Require(actual==start && entry<=fde+4+length);view.Extent(start,extent);
        if(extent)result.push_back({start,extent});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.address<b.address || (a.address==b.address && a.size<b.size);});
    result.erase(std::unique(result.begin(),result.end(),[](const auto& a,const auto& b){return a.address==b.address && a.size==b.size;}),result.end());
    for(std::size_t i=1;i<result.size();++i)Require(result[i].address>=result[i-1].address+result[i-1].size);
    return result;
}
}
