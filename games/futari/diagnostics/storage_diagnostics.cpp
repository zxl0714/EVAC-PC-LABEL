#include "futari_init.h"
#include <rex/runtime.h>
#include <rex/input/input.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {
uint32_t Read32(uint32_t address) {
  if (!address) return 0;
  auto* value = rex::Runtime::instance()->memory()->TranslateVirtual<rex::be<uint32_t>*>(address);
  return *value;
}

class FutariKeyTrace final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    REXLOG_INFO("Futari window key down: vk={} repeat={} previous={}",
        uint32_t(e.virtual_key()), e.repeat_count(), e.prev_state());
  }
  void OnKeyUp(rex::ui::KeyEvent& e) override {
    REXLOG_INFO("Futari window key up: vk={}", uint32_t(e.virtual_key()));
  }
};
std::string ReadName(uint32_t address, size_t limit) {
  if (!address) return "<null>";
  auto* name = rex::Runtime::instance()->memory()->TranslateVirtual<const char*>(address);
  return std::string(name, strnlen(name, limit));
}
thread_local uint32_t content_overlapped = 0;
thread_local uint32_t input_user = 0, input_state = 0;
struct InputPrevious {
  bool seen = false;
  uint32_t status = 0;
  uint16_t buttons = 0;
  int16_t lx = 0, ly = 0;
};
thread_local std::array<InputPrevious, 4> previous_input{};
bool InputTraceEnabled() {
  static const bool enabled = [] {
    const char* flag = std::getenv("FUTARI_INPUT_TRACE");
    return flag && std::string(flag) == "1";
  }();
  return enabled;
}
}

std::unique_ptr<rex::ui::WindowInputListener> FutariCreateInputTrace(rex::ui::Window* window) {
  if (!InputTraceEnabled()) return nullptr;
  auto listener = std::make_unique<FutariKeyTrace>();
  window->AddInputListener(listener.get(), 100000);
  return listener;
}

void FutariStorageSelectBegin(PPCRegister& user, PPCRegister& type, PPCRegister& flags,
    PPCRegister& size, PPCRegister& device, PPCRegister& overlapped) {
  REXLOG_INFO("Futari storage select begin: user={} type={} flags={:08X} bytes={} device_ptr={:08X} overlapped={:08X}",
      user.u32, type.u32, flags.u32, size.u64, device.u32, overlapped.u32);
}
void FutariStorageSelectReturn(PPCRegister& status, PPCRegister& object) {
  REXLOG_INFO("Futari storage select return: status={} object={:08X} device={} internal={} notification={}",
      status.u32, object.u32, Read32(object.u32 + 52), Read32(object.u32 + 24), Read32(object.u32 + 64));
}
void FutariStorageSelectComplete(PPCRegister& object) {
  REXLOG_INFO("Futari storage select complete: object={:08X} device={} internal={} extended={:08X} notification={} result={}",
      object.u32, Read32(object.u32 + 52), Read32(object.u32 + 24), Read32(object.u32 + 48),
      Read32(object.u32 + 64), Read32(object.u32 + 68));
}
void FutariStorageCreateBegin(PPCRegister& user, PPCRegister& root, PPCRegister& data,
    PPCRegister& flags, PPCRegister& disposition, PPCRegister& license,
    PPCRegister& cache, PPCRegister& size, PPCRegister& stack) {
  content_overlapped = Read32(stack.u32 + 84);
  REXLOG_INFO("Futari content create begin: user={} root='{}' device={} type={:08X} file='{}' flags={:08X} bytes={} cache={} overlapped={:08X}",
      user.u32, ReadName(root.u32, 64), data.u32 ? Read32(data.u32) : 0,
      data.u32 ? Read32(data.u32 + 4) : 0, data.u32 ? ReadName(data.u32 + 264, 42) : "<null>",
      flags.u32, size.u64, cache.u32, content_overlapped);
}
void FutariStorageCreateReturn(PPCRegister& status) {
  REXLOG_INFO("Futari content create return: status={} overlapped={:08X} internal={}",
      status.u32, content_overlapped, Read32(content_overlapped));
}
void FutariStorageDeviceCheck(PPCRegister& status, PPCRegister& stack) {
  auto* data = rex::Runtime::instance()->memory()->TranslateVirtual<rex::be<uint64_t>*>(stack.u32 + 96);
  REXLOG_INFO("Futari storage device check: status={} id={} free_bytes={}",
      status.u32, Read32(stack.u32 + 80), uint64_t(*data));
}
void FutariStorageSaveCheck(PPCRegister& save) {
  const auto global = Read32(0x821F8588);
  const auto system = global ? Read32(global + 76) : 0;
  const auto selector = system ? Read32(system + 84) : 0;
  REXLOG_INFO("Futari first-save precheck: selector={:08X} state={} device={}",
      selector, selector ? Read32(selector + 20) : 0, selector ? Read32(selector + 52) : 0);
}
void FutariStorageState(PPCRegister& object) {
  thread_local std::unordered_map<uint32_t, uint32_t> states;
  const auto state = Read32(object.u32 + 72);
  auto previous = states.find(object.u32);
  if (previous != states.end() && previous->second == state) return;
  if (states.size() >= 32) states.clear();
  states[object.u32] = state;
  const auto global = Read32(0x821F8588);
  const auto system = global ? Read32(global + 76) : 0;
  const auto selector = system ? Read32(system + 84) : 0;
  REXLOG_INFO("Futari storage state: object={:08X} handler={:08X} selector_state={} device={} mode={} system_device={} replay_device={}",
      object.u32, state, selector ? Read32(selector + 20) : 0, selector ? Read32(selector + 52) : 0,
      selector ? Read32(selector + 68) : 0, selector ? Read32(selector + 72) : 0, selector ? Read32(selector + 76) : 0);
}
void FutariSystemStorageState(PPCRegister& object) {
  thread_local uint32_t last_object = 0, last_state = 0;
  const auto state = Read32(object.u32 + 8284);
  if (last_object == object.u32 && last_state == state) return;
  last_object = object.u32;
  last_state = state;
  const auto global = Read32(0x821F8588);
  const auto system = global ? Read32(global + 76) : 0;
  const auto selector = system ? Read32(system + 84) : 0;
  REXLOG_INFO("Futari system storage state: object={:08X} handler={:08X} selector_state={} device={}",
      object.u32, state, selector ? Read32(selector + 20) : 0, selector ? Read32(selector + 52) : 0);
}
void FutariStorageIOState(PPCRegister& parent) {
  const auto object = Read32(parent.u32 + 16);
  if (!object) return;
  const auto state = Read32(object + 424);
  thread_local std::unordered_map<uint32_t, uint32_t> states;
  if (states.contains(object) && states[object] == state) return;
  if (states.size() >= 32) states.clear();
  states[object] = state;
  const auto global = Read32(0x821F8588);
  const auto system = global ? Read32(global + 76) : 0;
  const auto profile = system ? Read32(system + 76) : 0;
  REXLOG_INFO("Futari storage IO state: object={:08X} handler={:08X} op={} result={} device={} user={} file='{}' overlapped={}",
      object, state, Read32(object), Read32(object + 404), Read32(object + 16),
      profile ? Read32(profile + 40) : 0xFFFFFFFF, ReadName(object + 280, 42), Read32(object + 324));
}
void FutariStorageEnumResult(PPCRegister& status, PPCRegister& object, PPCRegister& stack) {
  if (status.u32 == 996) return;
  // SDK 0.9 returns ERROR_NO_MORE_FILES directly. Futari's content worker
  // expects asynchronous enumeration failure to be ERROR_FUNCTION_FAILED,
  // with the reason in ExtendedError. Translate only confirmed exhaustion;
  // leave permission, device, cancellation and other failures unchanged.
  const auto internal = Read32(object.u32 + 324);
  const auto count = Read32(object.u32 + 328);
  const auto extended = Read32(object.u32 + 348);
  if (status.u32 == 18 && internal == 18 && count == 0 && extended == 0x80070012) {
    status.u32 = 1627;
    REXLOG_INFO("Futari storage compatibility: empty content enumeration 18 -> 1627; extended reason preserved");
  }
  REXLOG_INFO("Futari storage enumerate complete: status={} internal={} extended={:08X} count={} op={}",
      status.u32, Read32(object.u32 + 324), Read32(object.u32 + 348),
      Read32(stack.u32 + 80), Read32(object.u32));
}
void FutariInputTraceBegin(PPCRegister& user, PPCRegister& state) {
  if (!InputTraceEnabled()) return;
  input_user = user.u32;
  input_state = state.u32;
}
void FutariInputTraceReturn(PPCRegister& status) {
  if (!InputTraceEnabled() || input_user >= 4 || !input_state) return;
  InputPrevious now;
  now.seen = true;
  now.status = status.u32;
  if (!status.u32) {
    auto* state = rex::Runtime::instance()->memory()->TranslateVirtual<rex::input::X_INPUT_STATE*>(input_state);
    now.buttons = state->gamepad.buttons;
    now.lx = state->gamepad.thumb_lx;
    now.ly = state->gamepad.thumb_ly;
  }
  auto& old = previous_input[input_user];
  if (!old.seen || now.status != old.status || now.buttons != old.buttons || now.lx != old.lx || now.ly != old.ly) {
    REXLOG_INFO("Futari input trace: user={} status={} buttons={:04X} lx={} ly={}",
        input_user, now.status, now.buttons, now.lx, now.ly);
    old = now;
  }
}
