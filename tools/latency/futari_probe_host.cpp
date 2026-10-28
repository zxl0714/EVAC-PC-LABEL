#include "futari_probe_exports.h"
#include "latency_probe.h"
#include "test_pad.h"
#include <rex/runtime.h>
#include <rex/memory.h>

extern "C" __declspec(dllexport) std::uint64_t EvacLatencyRecord(std::uint32_t kind,
    std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
    std::uint64_t e, std::uint64_t f) {
  return evac_latency::Record(static_cast<evac_latency::Kind>(kind), a, b, c, d, e, f);
}
void FutariProbeInitialize() {
  if (evac_latency::Initialize()) evac_latency::InitializeTestPad();
}
extern "C" __declspec(dllexport) std::uint64_t EvacLatencyTestCommand() {
  return evac_latency::CurrentTestCommand();
}
void FutariProbeFlush() {
  using PixelFlush = void (*)();
  const auto runtime = GetModuleHandleW(L"rexruntime.dll");
  const auto flush = runtime ? reinterpret_cast<PixelFlush>(
      GetProcAddress(runtime, "EvacLatencyPixelsFlush")) : nullptr;
  if (flush) flush();
  evac_latency::Flush();
}
bool FutariProbeInput(rex::input::X_INPUT_STATE* output) {
  if (!evac_latency::test_pad) return false;
  // Commands 900..999 keep pixel capture enabled while passing actual window
  // keyboard state through the original configured driver for frontend tests.
  if (evac_latency::CurrentTestCommand() >= 900) return false;
  using namespace evac_latency;
  const auto begin = Record(Kind::InputBegin, 0);
  const auto input = ReadTestPad();
  Record(Kind::SyntheticSample, input.event, 0, input.event, input.buttons);
  *output = {};
  output->packet_number = static_cast<std::uint32_t>(input.event);
  output->gamepad.buttons = input.buttons;
  Record(Kind::InputEnd, begin, 0, 0, output->packet_number, output->gamepad.buttons);
  return true;
}
void FutariProbeSwap(std::uint32_t buffer) {
  if (!evac_latency::state) return;
  const auto physical = rex::Runtime::instance()->memory()->GetPhysicalAddress(buffer);
  if (physical != 0xffffffffu && physical <= 0xffffffe3u)
    evac_latency::Record(evac_latency::Kind::GuestSwap, physical + 28);
}
