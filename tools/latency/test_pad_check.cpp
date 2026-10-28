#include "test_pad.h"
#include <iostream>

int main() {
  const auto name = L"EVACLatencyCheck-" + std::to_wstring(GetCurrentProcessId());
  SetEnvironmentVariableW(L"EVAC_LATENCY_TEST_PAD", name.c_str());
  if (!evac_latency::Initialize() || !evac_latency::InitializeTestPad()) return 2;
  const auto path = L"\\\\.\\pipe\\" + name;
  HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  const char command[] = "1 100 1000\n";
  DWORD count = 0;
  if (!WriteFile(pipe, command, sizeof(command) - 1, &count, nullptr)) return 4;
  char response[32]{};
  if (!ReadFile(pipe, response, sizeof(response), &count, nullptr) ||
      std::string_view(response, count) != "OK\n") return 5;
  CloseHandle(pipe);
  const auto down = evac_latency::ReadTestPad();
  if (down.buttons != 0x1000 || !down.event) return 6;
  Sleep(150);
  const auto up = evac_latency::ReadTestPad();
  if (up.buttons || up.event <= down.event) return 7;
  std::cout << "Synthetic test input: acknowledgement, down state, release state and distinct source IDs passed\n";
}
