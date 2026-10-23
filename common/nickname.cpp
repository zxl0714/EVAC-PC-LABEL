#include "common/nickname.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace cave {
std::string NormalizeDisplayNickname(std::string_view name) {
    if(name.empty() || name.size()>512)return {};
    auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),nullptr,0);
    if(size<=0)return {};
    std::wstring wide(size,L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),wide.data(),size)!=size)return {};
    size=NormalizeString(NormalizationKC,wide.data(),int(wide.size()),nullptr,0);
    if(size<=0 || size>512)return {};
    std::wstring normalized(size,L'\0');
    size=NormalizeString(NormalizationKC,wide.data(),int(wide.size()),normalized.data(),size);
    if(size<=0)return {};
    normalized.resize(size);
    auto space=[](wchar_t ch) {WORD type=0;return GetStringTypeW(CT_CTYPE1,&ch,1,&type) && (type&C1_SPACE);};
    const auto first=std::find_if_not(normalized.begin(),normalized.end(),space);
    const auto last=std::find_if_not(normalized.rbegin(),normalized.rend(),space).base();
    if(first>=last)return {};
    const std::wstring_view trimmed(&*first,size_t(last-first));
    size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,trimmed.data(),int(trimmed.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string result(size,'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,trimmed.data(),int(trimmed.size()),result.data(),size,nullptr,nullptr)!=size)return {};
    return result;
}
std::string LegacyDisplayNickname(std::string_view name) {
    if(!name.empty() && std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32 && c<=126;}))
        return std::string(name.substr(0,15));
    std::array<unsigned char,32> hash{};
    if(name.size()>512 || BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(name.data())),ULONG(name.size()),hash.data(),ULONG(hash.size()))<0)
        throw std::runtime_error("Cannot calculate native nickname preview");
    constexpr char digits[]="0123456789abcdef";
    std::string result="Player-";
    for(unsigned index=0;index<4;++index) {result+=digits[hash[index]>>4];result+=digits[hash[index]&15];}
    return result;
}
std::string Ps4DisplayNickname(std::string_view name) {
    auto end=(std::min)(name.size(),size_t{16});
    while(end<name.size() && end && (static_cast<unsigned char>(name[end])&0xc0)==0x80)--end;
    return std::string(name.substr(0,end));
}
}
