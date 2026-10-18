// Base64/hash helpers adapted from Futari community_runtime.cpp, commit
// 7dced5979076329db71b1296c81944c83fb72b48. G2 envelope is independently parsed.
#include "community/replay_format.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdexcept>

namespace espgaluda2 {
namespace {
void Check(bool value) { if (!value) throw std::runtime_error("Invalid G2 replay"); }
uint32_t Be32(std::span<const uint8_t> b, size_t at) {
  return uint32_t(b[at])<<24 | uint32_t(b[at+1])<<16 | uint32_t(b[at+2])<<8 | b[at+3];
}
uint64_t Be64(std::span<const uint8_t> b, size_t at) { return uint64_t(Be32(b,at))<<32 | Be32(b,at+4); }
}
ReplayInfo InspectReplay(std::span<const uint8_t> record) {
  Check(record.size()>kReplayHeaderBytes && record.size()<=kReplayMaxBytes);
  // Word 0 is the title's local-save checksum, not a size. Native network
  // uploads leave it zero; 8209D640 fills it only for the later local save.
  // Transport integrity is checked independently with SHA-256.
  Check(Be32(record,4)==2);
  Check(uint64_t(Be32(record,96))+kReplayHeaderBytes==record.size());
  const auto time=Be64(record,8), score=Be64(record,16);
  const auto character=Be32(record,36);
  Check(time>0 && time<=INT64_MAX && score<=INT64_MAX && character<=2);
  return {time,int64_t(score),Be32(record,24),character};
}
std::string ReplaySha256(std::span<const uint8_t> record) {
  Check(!record.empty() && record.size()<=kReplayMaxBytes);
  uint8_t hash[32];
  Check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(record.data()),ULONG(record.size()),hash,32)>=0);
  constexpr char hex[]="0123456789abcdef";
  std::string result;
  for(auto byte:hash) { result+=hex[byte>>4]; result+=hex[byte&15]; }
  return result;
}
std::string ReplayBase64(std::span<const uint8_t> record) {
  Check(!record.empty() && record.size()<=kReplayMaxBytes);
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
  Check(!text.empty() && text.size()<=(kReplayMaxBytes+2)/3*4 && text.size()%4==0 &&
      text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")==text.npos);
  DWORD count=0;
  constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_STRICT;
  Check(CryptStringToBinaryA(text.data(),DWORD(text.size()),flags,nullptr,&count,nullptr,nullptr));
  Check(count>0 && count<=kReplayMaxBytes);
  std::vector<uint8_t> result(count);
  Check(CryptStringToBinaryA(text.data(),DWORD(text.size()),flags,result.data(),&count,nullptr,nullptr));
  result.resize(count);
  Check(ReplayBase64(result)==text);
  return result;
}
}  // namespace espgaluda2
