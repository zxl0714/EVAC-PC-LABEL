// Shared lossless replay transport encoding, factored from the maintained G2 adapter.
#include "community/replay_encoding.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdexcept>
namespace cave::community {
namespace {void Check(bool ok){if(!ok)throw std::runtime_error("Invalid replay encoding");}}
std::string ReplaySha256(std::span<const uint8_t> record) {
  Check(!record.empty() && record.size()<=kReplayEncodingLimit);
  uint8_t hash[32];
  Check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(record.data()),ULONG(record.size()),hash,32)>=0);
  constexpr char hex[]="0123456789abcdef";
  std::string result;
  for(auto byte:hash) { result+=hex[byte>>4]; result+=hex[byte&15]; }
  return result;
}
std::string ReplayBase64(std::span<const uint8_t> record) {
  Check(!record.empty() && record.size()<=kReplayEncodingLimit);
  DWORD count=0;
  constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
  Check(CryptBinaryToStringA(record.data(),DWORD(record.size()),flags,nullptr,&count));
  std::string result(count,'\0');
  Check(CryptBinaryToStringA(record.data(),DWORD(record.size()),flags,result.data(),&count));
  result.resize(count);
  while(!result.empty() && result.back()=='\0')result.pop_back();
  return result;
}
std::vector<uint8_t> ReplayUnbase64(const std::string& text) {
  Check(!text.empty() && text.size()<=(kReplayEncodingLimit+2)/3*4 && text.size()%4==0 &&
      text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")==text.npos);
  DWORD count=0;
  constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_STRICT;
  Check(CryptStringToBinaryA(text.data(),DWORD(text.size()),flags,nullptr,&count,nullptr,nullptr));
  Check(count>0 && count<=kReplayEncodingLimit);
  std::vector<uint8_t> result(count);
  Check(CryptStringToBinaryA(text.data(),DWORD(text.size()),flags,result.data(),&count,nullptr,nullptr));
  result.resize(count);
  Check(ReplayBase64(result)==text);
  return result;
}
}

