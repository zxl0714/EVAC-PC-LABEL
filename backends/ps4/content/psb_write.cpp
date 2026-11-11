#include "resource_prepare.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json=nlohmann::json;
using Bytes=ResourceBytes;
constexpr auto kLimit=32u*1024*1024;

void Require(bool okay,const char* message) {if(!okay)throw std::runtime_error(message);}
void Put(Bytes& bytes,std::uint64_t value,unsigned width) {
    for(unsigned index=0;index<width;++index)bytes.push_back(static_cast<std::uint8_t>(value>>(8*index)));
}
void Write(Bytes& bytes,std::size_t offset,std::uint32_t value) {
    for(unsigned index=0;index<4;++index)bytes.at(offset+index)=static_cast<std::uint8_t>(value>>(8*index));
}
unsigned Width(std::uint32_t value) {
    return (std::max)(1u,(static_cast<unsigned>(std::bit_width(value))+7)/8);
}
void Array(Bytes& bytes,std::span<const std::uint32_t> values) {
    const auto count=static_cast<std::uint32_t>(values.size());
    const auto count_width=Width(count);
    const auto item_width=values.empty()?1:Width(*std::ranges::max_element(values));
    bytes.push_back(static_cast<std::uint8_t>(12+count_width));Put(bytes,count,count_width);
    bytes.push_back(static_cast<std::uint8_t>(12+item_width));
    for(const auto value:values)Put(bytes,value,item_width);
}
void Append(Bytes& bytes,std::span<const std::uint8_t> more) {
    Require(bytes.size()<=kLimit && more.size()<=kLimit-bytes.size(),"PSB output exceeds its size limit");
    bytes.insert(bytes.end(),more.begin(),more.end());
}
std::uint32_t Position(const Bytes& bytes) {return static_cast<std::uint32_t>(bytes.size());}

struct TrieNode {
    std::map<std::uint8_t,std::uint32_t> children;
    std::uint32_t name=UINT32_MAX;
};
class Names {
public:
    explicit Names(const std::map<std::string,std::uint32_t>& names):terminals_(names.size()) {
        for(const auto& [name,id]:names) {
            std::uint32_t node=0;
            for(const unsigned char letter:name) {
                const auto child=static_cast<std::uint32_t>(nodes_.size());
                const auto [item,new_child]=nodes_[node].children.try_emplace(letter,child);
                node=item->second;
                if(new_child)nodes_.emplace_back();
            }
            nodes_[node].name=id;
        }
        for(std::size_t index=0;index<pending_.size();++index) {
            const auto [node,slot]=pending_[index];
            Place(nodes_[node],slot);
        }
    }
    void Encode(Bytes& output) const {
        Array(output,charset_);Array(output,parents_);Array(output,terminals_);
    }
private:
    void Extend(std::uint32_t last) {
        Require(last<1000000,"PSB name trie exceeds its size limit");
        if(last<used_.size())return;
        used_.resize(last+1);charset_.resize(last+1);parents_.resize(last+1);
    }
    void Place(const TrieNode& node,std::uint32_t slot) {
        const bool terminal=node.name!=UINT32_MAX;
        const auto last=node.children.empty()?0:node.children.rbegin()->first;
        auto base=next_;
        for(;;++base) {
            Extend(base+last);
            if(terminal && used_[base])continue;
            if(std::ranges::none_of(node.children,[&](const auto& item){return used_[base+item.first];}))break;
        }
        next_=base+1;
        charset_[slot]=base;
        if(terminal) {
            used_[base]=true;parents_[base]=slot;
            charset_[base]=node.name;terminals_[node.name]=base;
        }
        for(const auto& [letter,child]:node.children) {
            const auto index=base+letter;
            used_[index]=true;parents_[index]=slot;
            pending_.emplace_back(child,index);
        }
    }
    std::vector<TrieNode> nodes_{1};
    std::vector<std::pair<std::uint32_t,std::uint32_t>> pending_{{0,0}};
    std::vector<bool> used_{true};
    std::vector<std::uint32_t> charset_{0},parents_{0},terminals_;
    std::uint32_t next_=1;
};

class PlainPsbWriter {
public:
    Bytes WriteDocument(const Json& value,std::uint32_t header_length) {
        Require(value.is_object() && (header_length==0 || header_length==40),"Expected a plain PSB v2 object");
        CollectNames(value,0);
        std::uint32_t key=0;
        for(auto& entry:names_)entry.second=key++;

        Bytes output{'P','S','B',0,2,0,0,0};
        output.resize(40);
        Write(output,8,header_length);
        Write(output,12,Position(output));
        Names(names_).Encode(output);
        Write(output,36,Position(output));
        Append(output,EncodeValue(value));

        std::vector<std::uint32_t> offsets;
        Bytes strings;
        for(const auto& text:strings_) {
            offsets.push_back(Position(strings));
            Append(strings,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
            strings.push_back(0);
        }
        Write(output,16,Position(output));Array(output,offsets);
        Write(output,20,Position(output));Append(output,strings);
        Write(output,24,Position(output));Array(output,{});
        Write(output,28,Position(output));Array(output,{});
        Write(output,32,Position(output));
        return output;
    }
private:
    void CollectNames(const Json& value,unsigned depth) {
        Require(depth<=64 && ++nodes_<=100000,"PSB output has too many values");
        if(value.is_array()) {
            for(const auto& item:value)CollectNames(item,depth+1);
        } else if(value.is_object()) {
            Require(!value.contains("resource_index"),"Resource references require an existing PSB chunk table");
            for(auto item=value.begin();item!=value.end();++item) {
                const auto& name=item.key();
                Require(!name.empty() && name.size()<=4096 && name.find('\0')==name.npos,"Invalid PSB output name");
                names_.try_emplace(name,0);
                CollectNames(item.value(),depth+1);
            }
        }
    }
    Bytes EncodeValue(const Json& value) {
        if(value.is_null())return {1};
        if(value.is_boolean())return {static_cast<std::uint8_t>(value.get<bool>()?3:2)};
        if(value.is_number_integer()) {
            Require(!value.is_number_unsigned() || value.get<std::uint64_t>()<=INT64_MAX,"PSB integer exceeds signed range");
            const auto number=value.get<std::int64_t>();
            if(!number)return {4};
            unsigned width=1;
            for(;width<8;++width) {
                const auto half=std::int64_t{1}<<(8*width-1);
                if(number>=-half && number<half)break;
            }
            Bytes bytes{static_cast<std::uint8_t>(4+width)};
            Put(bytes,static_cast<std::uint64_t>(number),width);
            return bytes;
        }
        if(value.is_number_float()) {
            const auto number=value.get<double>();
            const auto single=static_cast<float>(number);
            const bool exact=static_cast<double>(single)==number;
            Bytes bytes{static_cast<std::uint8_t>(exact?30:31)};
            if(exact)Put(bytes,std::bit_cast<std::uint32_t>(single),4);
            else Put(bytes,std::bit_cast<std::uint64_t>(number),8);
            return bytes;
        }
        if(value.is_string()) {
            const auto& text=value.get_ref<const std::string&>();
            Require(text.size()<=kLimit && text.find('\0')==text.npos,"Invalid PSB output string");
            const auto [item,inserted]=string_indexes_.try_emplace(text,static_cast<std::uint32_t>(strings_.size()));
            if(inserted)strings_.push_back(text);
            const auto width=Width(item->second);
            Bytes bytes{static_cast<std::uint8_t>(20+width)};
            Put(bytes,item->second,width);
            return bytes;
        }

        Require(value.is_array() || value.is_object(),"Unsupported PSB output value");
        Bytes bytes{static_cast<std::uint8_t>(value.is_object()?33:32)},children;
        std::vector<std::uint32_t> names,offsets;
        for(auto item=value.begin();item!=value.end();++item) {
            if(value.is_object())names.push_back(names_.at(item.key()));
            offsets.push_back(Position(children));
            Append(children,EncodeValue(item.value()));
        }
        if(value.is_object())Array(bytes,names);
        Array(bytes,offsets);
        Append(bytes,children);
        return bytes;
    }
    unsigned nodes_{};
    std::map<std::string,std::uint32_t> names_,string_indexes_;
    std::vector<std::string> strings_;
};
}
ResourceBytes WritePlainPsb(const nlohmann::json& value,std::uint32_t header_length) {
    return PlainPsbWriter().WriteDocument(value,header_length);
}
}
