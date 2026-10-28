#pragma once

// Explicit synthetic guest-input source for isolated tests, not a keyboard or
// USB latency measurement. Disabled unless EVAC_LATENCY_TEST_PAD names a pipe.
// One worker sleeps between commands; the game's input path performs one load.
#include "latency_probe.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace evac_latency {
struct TestPad {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  std::atomic<uint64_t> published{0};

  void Publish(uint64_t command, unsigned buttons, unsigned phase) {
    const auto event = Record(Kind::SyntheticInput, command, buttons, phase);
    published.store((event << 16) | buttons, std::memory_order_release);
  }

  void Run() {
    for (;;) {
      if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
      char message[128]{};
      DWORD count = 0;
      unsigned long long command = 0;
      unsigned duration = 0, buttons = 0;
      char extra = 0;
      const bool valid = ReadFile(pipe, message, sizeof(message) - 1, &count, nullptr) &&
          count && sscanf_s(message, "%llu %u %x %c", &command, &duration, &buttons, &extra, 1u) == 3 &&
          command && duration >= 1 && duration <= 2000 && buttons <= 0xFFFF;
      const char* response = valid ? "OK\n" : "INVALID\n";
      DWORD written = 0;
      if (valid) Publish(command, buttons, 1);
      WriteFile(pipe, response, static_cast<DWORD>(std::strlen(response)), &written, nullptr);
      FlushFileBuffers(pipe);
      // Requested hold time is only for navigation. Timing uses the actual
      // Publish timestamps, including any acknowledgement or scheduler delay.
      DisconnectNamedPipe(pipe);
      if (valid) {
        Sleep(duration);
        Publish(command, 0, 0);
      }
    }
  }
};

inline TestPad* test_pad = nullptr;

inline bool InitializeTestPad() {
  wchar_t name[128]{};
  const auto length = GetEnvironmentVariableW(L"EVAC_LATENCY_TEST_PAD", name, 128);
  if (!length) return false;
  if (!state || length >= 128) throw std::runtime_error("Test pad requires a fresh latency trace");
  for (unsigned i = 0; i < length; ++i)
    if (!(name[i] >= L'A' && name[i] <= L'Z') && !(name[i] >= L'a' && name[i] <= L'z') &&
        !(name[i] >= L'0' && name[i] <= L'9') && name[i] != L'-' && name[i] != L'_')
      throw std::runtime_error("Invalid test pad pipe name");
  auto* pad = new TestPad;
  const auto path = std::wstring(L"\\\\.\\pipe\\") + name;
  pad->pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, 128, 128, 0, nullptr);
  if (pad->pipe == INVALID_HANDLE_VALUE) {
    delete pad;
    throw std::runtime_error("Cannot create isolated test pad pipe");
  }
  test_pad = pad;
  pad->Publish(0, 0, 0);
  std::thread([pad] { pad->Run(); }).detach();
  return true;
}

struct TestPadSample {
  uint64_t event;
  uint16_t buttons;
};

inline TestPadSample ReadTestPad() {
  const auto value = test_pad->published.load(std::memory_order_acquire);
  return {value >> 16, static_cast<uint16_t>(value)};
}

inline uint64_t CurrentTestCommand() {
  if (!test_pad || !state) return 0;
  const auto event = ReadTestPad().event;
  if (!event || event > state->capacity) return 0;
  const auto& row = state->events[event - 1];
  return row.ready.load(std::memory_order_acquire) &&
      row.kind == static_cast<uint32_t>(Kind::SyntheticInput) ? row.values[0] : 0;
}
}  // namespace evac_latency
