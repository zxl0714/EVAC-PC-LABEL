// HTTP transport extracted from Mushihimesama-Futari-PC, commit
// 7dced5979076329db71b1296c81944c83fb72b48 (src/community_client.cpp).
// Game-specific stats/replay layouts are deliberately not imported here.
#include "community/http_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string_view>

#include <rex/logging.h>

namespace cave::community {
namespace {
std::wstring Wide(std::string_view value) {
  if (value.size() > 8192) return {};
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
  if (!count) return {};
  std::wstring out(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), out.data(), count);
  return out;
}

struct HttpHandle {
  HINTERNET value = nullptr;
  ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
}  // namespace

bool ValidEndpoint(std::string_view endpoint) {
  if (endpoint.empty() || endpoint.find_first_of("\\ \t\r\n?#") != endpoint.npos) return false;
  auto url = Wide(endpoint);
  URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
  parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwExtraInfoLength = DWORD(-1);
  if (url.empty() || !WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts)
      || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength)
    return false;
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return c>=L'A' && c<=L'Z'?c-L'A'+L'a':c;});
  return parts.nScheme == INTERNET_SCHEME_HTTPS ||
      (parts.nScheme == INTERNET_SCHEME_HTTP && (host==L"127.0.0.1" || host==L"localhost" || host==L"[::1]" || host==L"::1"));
}

std::string NormalizeNickname(std::string_view name) {
  if(name.empty() || name.size()>512)return {};
  const auto wide=Wide(name);
  if(wide.empty())return {};
  auto size=NormalizeString(NormalizationKC,wide.data(),int(wide.size()),nullptr,0);
  if(size<=0 || size>512)return {};
  std::wstring normalized(size,L'\0');
  size=NormalizeString(NormalizationKC,wide.data(),int(wide.size()),normalized.data(),size);
  if(size<=0)return {};
  normalized.resize(size);
  auto space=[](wchar_t ch) { WORD type=0;return GetStringTypeW(CT_CTYPE1,&ch,1,&type) && (type&C1_SPACE); };
  auto first=std::find_if_not(normalized.begin(),normalized.end(),space);
  auto last=std::find_if_not(normalized.rbegin(),normalized.rend(),space).base();
  if(first>=last)return {};
  std::wstring_view trimmed(&*first,size_t(last-first));
  size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,trimmed.data(),int(trimmed.size()),nullptr,0,nullptr,nullptr);
  if(size<=0)return {};
  std::string result(size,'\0');
  if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,trimmed.data(),int(trimmed.size()),result.data(),size,nullptr,nullptr))return {};
  return result;
}

bool ValidNickname(std::string_view name) {
  // 48 UTF-8 bytes is also enforced by the server. Reject invisible controls,
  // invalid UTF-8 and edge whitespace before any game/session is started.
  if (name.empty() || name.size() > 48 || name.front() == ' ' || name.back() == ' ') return false;
  const auto wide = Wide(name);
  if (wide.empty() || wide.size() > 32) return false;
  for (wchar_t c : wide)
    if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e)
        || (c >= 0x2060 && c <= 0x206f) || c == 0xfeff) return false;
  return true;
}

HttpClient::HttpClient(std::string endpoint) : endpoint_(std::move(endpoint)) {
  while (!endpoint_.empty() && endpoint_.back()=='/') endpoint_.pop_back();
  if (!ValidEndpoint(endpoint_)) throw std::runtime_error("Invalid community endpoint; use HTTPS or loopback HTTP");
  worker_ = std::thread([this] { Run(); });
}
HttpClient::~HttpClient() { Stop(); }

void HttpClient::Stop() {
  std::lock_guard stop_lock(stop_mutex_);
  stopping_ = true;
  {
    std::lock_guard lock(mutex_);
    if (active_request_) {
      WinHttpCloseHandle(static_cast<HINTERNET>(active_request_));
      active_request_ = nullptr;
    }
    while (!jobs_.empty()) {
      jobs_.front().completion.set_value({0, ERROR_CANCELLED, {}});
      jobs_.pop_front();
    }
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

std::future<HttpResult> HttpClient::Request(std::string path, nlohmann::json body) {
  Job job{std::move(path), std::move(body)};
  auto future = job.completion.get_future();
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || jobs_.size() >= 16) {
      job.completion.set_value({0, unsigned(stopping_ ? ERROR_CANCELLED : ERROR_BUSY), {}});
      return future;
    }
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
  return future;
}

void HttpClient::Run() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    auto started = std::chrono::steady_clock::now();
    HttpResult result;
    try { result = Perform(job); }
    catch (...) { result.error = ERROR_INVALID_DATA; }
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    // Log only the operation, HTTP status/error and timing. Never nickname,
    // machine identity, payload, full URL or replay data.
    auto operation = job.path.substr(0, job.path.find('?'));
    if (operation.starts_with("/replays/")) operation = "/replays/:id";
    REXLOG_INFO("Community request={} status={} error={} ms={}", operation, result.status, result.error, duration);
    job.completion.set_value(std::move(result));
  }
}

HttpResult HttpClient::Perform(const Job& job) {
  HttpResult result;
  if (job.path.empty() || job.path.size()>8192 || job.path.front() != '/' || job.path.starts_with("//")
      || job.path.find_first_of("\r\n#\\") != std::string::npos)
    return {0, ERROR_INVALID_PARAMETER, {}};
  std::wstring url = Wide(endpoint_ + job.path);
  URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
  if (url.empty() || !WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts)) return {0, ERROR_INVALID_PARAMETER, {}};
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  // TLS is mandatory except an explicitly configured loopback test server.
  std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return c>=L'A' && c<=L'Z'?c-L'A'+L'a':c;});
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  if ((!secure && !(parts.nScheme == INTERNET_SCHEME_HTTP && (host == L"127.0.0.1" || host == L"localhost" || host == L"[::1]" || host == L"::1")))
      || parts.dwUserNameLength || parts.dwPasswordLength) return {0, ERROR_INVALID_PARAMETER, {}};
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  HttpHandle session{WinHttpOpen(L"CaveCommunity/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.value) return {0, GetLastError(), {}};
  WinHttpSetTimeouts(session.value, 2000, 2000, 3000, 3000);
  HttpHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
  if (!connection.value) return {0, GetLastError(), {}};
  HINTERNET request = WinHttpOpenRequest(connection.value, job.body.is_null() ? L"GET" : L"POST", path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request) return {0, GetLastError(), {}};
  {
    std::lock_guard lock(mutex_);
    if (stopping_) { WinHttpCloseHandle(request); return {0, ERROR_CANCELLED, {}}; }
    active_request_ = request;
  }
  auto release_request = [this, request](void*) {
    std::lock_guard lock(mutex_);
    if (active_request_ == request) { WinHttpCloseHandle(request); active_request_ = nullptr; }
  };
  std::unique_ptr<void, decltype(release_request)> request_guard(request, release_request);
  DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
  // Avoid automatically sending Windows credentials to any configured host.
  DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon));
  std::string payload = job.body.is_null() ? "" : job.body.dump();
  if (payload.size() > 12 * 1024 * 1024) return {0, ERROR_FILE_TOO_LARGE, {}};
  if (!WinHttpSendRequest(request, L"Content-Type: application/json\r\n", DWORD(-1),
                          payload.empty() ? nullptr : payload.data(), DWORD(payload.size()), DWORD(payload.size()), 0)
      || !WinHttpReceiveResponse(request, nullptr)) return {0, GetLastError(), {}};
  DWORD status = 0, length = sizeof(status);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
    return {0, GetLastError(), {}};
  result.status = status;
  std::string response;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  std::array<char, 16384> bytes{};
  while (!stopping_) {
    DWORD read = 0;
    if (!WinHttpReadData(request, bytes.data(), DWORD(bytes.size()), &read)) return {status, GetLastError(), {}};
    if (!read) break;
    if (response.size() + read > 12 * 1024 * 1024 || std::chrono::steady_clock::now() > deadline)
      return {status, ERROR_BUFFER_OVERFLOW, {}};
    response.append(bytes.data(), read);
  }
  if (stopping_) return {status, ERROR_CANCELLED, {}};
  result.body = nlohmann::json::parse(response, nullptr, false);
  if (result.body.is_discarded() || !result.body.is_object()) return {status, ERROR_INVALID_DATA, {}};
  return result;
}
}  // namespace cave::community
