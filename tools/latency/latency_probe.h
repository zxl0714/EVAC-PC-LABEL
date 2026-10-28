#pragma once

// Measurement-only Windows probe. No clock scaling, polling or input injection.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <new>
#include <string>

namespace evac_latency {

enum class Kind : uint32_t {
  Startup = 1,
  WindowKey = 2,
  InputBegin = 3,
  InputEnd = 4,
  DriverState = 5,
  SwapPacket = 6,
  OutputReady = 7,
  PresentBegin = 8,
  PresentEnd = 9,
  GuestObservation = 10,
  BindingApplied = 11,
  BindingSample = 12,
  GuestSwap = 13,
  SyntheticInput = 14,
  SyntheticSample = 15,
  DxgiFrameStatistics = 16,
  DxgiRefreshStatistics = 17,
  Ps4PadBegin = 18,
  Ps4PadEnd = 19,
  Ps4PadReport = 20,
  Ps4FrameReady = 21,
  Ps4PresentBegin = 22,
  Ps4PresentEnd = 23,
  Ps4SdlEvent = 24,
  Ps4ControllerUpdate = 25,
  Ps4FramePrepare = 26,
  Ps4ControllerSample = 27,
};

constexpr uint64_t kCapacity = 262144;
struct Event {
  std::atomic<bool> ready{false};
  uint32_t kind = 0;
  uint32_t thread = 0;
  uint64_t qpc = 0;
  uint64_t values[6]{};
};
struct State {
  std::atomic<bool> accepting{true};
  std::atomic<uint64_t> reserved{0};
  Event* events = nullptr;
  HANDLE output = INVALID_HANDLE_VALUE;
  uint64_t frequency = 0;
  uint64_t capacity = kCapacity;
};
// Intentionally retained until process termination: late teardown writers may
// still own an event when the exit handler takes its committed-record snapshot.
inline State* state = nullptr;
inline thread_local uint64_t producer_swap = 0;

inline uint64_t Record(Kind kind, uint64_t a = 0, uint64_t b = 0,
                       uint64_t c = 0, uint64_t d = 0, uint64_t e = 0,
                       uint64_t f = 0) noexcept {
  auto* s = state;
  if (!s || !s->accepting.load(std::memory_order_relaxed)) return 0;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const uint64_t index = s->reserved.fetch_add(1, std::memory_order_relaxed);
  if (index >= s->capacity) return 0;
  auto& event = s->events[index];
  event.kind = static_cast<uint32_t>(kind);
  event.thread = GetCurrentThreadId();
  event.qpc = static_cast<uint64_t>(now.QuadPart);
  event.values[0] = a; event.values[1] = b; event.values[2] = c;
  event.values[3] = d; event.values[4] = e; event.values[5] = f;
  event.ready.store(true, std::memory_order_release);
  return index + 1;
}

inline bool Write(const std::string& text) noexcept {
  DWORD written = 0;
  return WriteFile(state->output, text.data(), static_cast<DWORD>(text.size()),
                   &written, nullptr) && written == text.size();
}

inline void Flush() noexcept {
  auto* s = state;
  if (!s || !s->accepting.exchange(false)) return;
  const DWORD saved_error = GetLastError();
  const uint64_t reserved = s->reserved.load(std::memory_order_acquire);
  const uint64_t count = reserved < s->capacity ? reserved : s->capacity;
  uint64_t committed = 0, incomplete = 0;
  try {
    std::string buffer = "# evac_latency_v1 frequency=" + std::to_string(s->frequency) +
        " pid=" + std::to_string(GetCurrentProcessId()) + " capacity=" +
        std::to_string(s->capacity) + "\nseq,qpc,thread,kind,a,b,c,d,e,f\n";
    bool ok = true;
    for (uint64_t i = 0; i < count; ++i) {
      const auto& event = s->events[i];
      if (!event.ready.load(std::memory_order_acquire)) { ++incomplete; continue; }
      ++committed;
      char line[256];
      const int size = std::snprintf(line, sizeof(line),
          "%llu,%llu,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu\n",
          static_cast<unsigned long long>(i + 1),
          static_cast<unsigned long long>(event.qpc), event.thread, event.kind,
          static_cast<unsigned long long>(event.values[0]),
          static_cast<unsigned long long>(event.values[1]),
          static_cast<unsigned long long>(event.values[2]),
          static_cast<unsigned long long>(event.values[3]),
          static_cast<unsigned long long>(event.values[4]),
          static_cast<unsigned long long>(event.values[5]));
      if (size <= 0 || static_cast<size_t>(size) >= sizeof(line)) { ok = false; break; }
      buffer.append(line, size);
      if (buffer.size() >= 1024 * 1024) {
        if (!Write(buffer)) { ok = false; break; }
        buffer.clear();
      }
    }
    if (ok) {
      buffer += "# end reserved=" + std::to_string(reserved) +
          " committed=" + std::to_string(committed) +
          " incomplete=" + std::to_string(incomplete) +
          " overflow=" + std::to_string(reserved - count) + "\n";
      Write(buffer);
    }
  } catch (...) { /* A missing footer is a failed/incomplete capture. */ }
  CloseHandle(s->output);
  s->output = INVALID_HANDLE_VALUE;
  SetLastError(saved_error);
}

inline bool Initialize() noexcept {
  if (state) return true;
  const DWORD saved_error = GetLastError();
  wchar_t path[32768];
  const DWORD length = GetEnvironmentVariableW(L"EVAC_LATENCY_TRACE", path, 32768);
  if (!length || length >= 32768) { SetLastError(saved_error); return false; }
  auto* candidate = new (std::nothrow) State;
  if (!candidate) { SetLastError(saved_error); return false; }
  wchar_t capacity_text[32]{};
  const auto capacity_length = GetEnvironmentVariableW(L"EVAC_LATENCY_CAPACITY", capacity_text, 32);
  if (capacity_length) {
    wchar_t* end = nullptr;
    const auto capacity = std::wcstoull(capacity_text, &end, 10);
    if (capacity_length >= 32 || !end || *end || capacity < 16 || capacity > 2097152) {
      delete candidate; SetLastError(saved_error); return false;
    }
    candidate->capacity = capacity;
  }
  candidate->events = new (std::nothrow) Event[candidate->capacity];
  if (!candidate->events) { delete candidate; SetLastError(saved_error); return false; }
  candidate->output = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (candidate->output == INVALID_HANDLE_VALUE) {
    delete[] candidate->events; delete candidate;
    SetLastError(saved_error); return false;
  }
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  candidate->frequency = static_cast<uint64_t>(frequency.QuadPart);
  state = candidate;
  std::atexit(Flush);
  Record(Kind::Startup, GetCurrentProcessId());
  SetLastError(saved_error);
  return true;
}

}  // namespace evac_latency
