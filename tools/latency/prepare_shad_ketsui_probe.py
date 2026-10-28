"""Prepare isolated shadPS4 core/ROI observations without modifying its baseline."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project import digest,write_json_new
from prepare_ps4_native_probe import replace_one


def pixel_header(text):
    text=replace_one(text,'#include "ps4_probe_exports.h"',
        '#include "ps4_probe.h"\n#include "video_core/renderer_vulkan/vk_instance.h"')
    begin=text.index('inline std::uint64_t HostCommand() {')
    end=text.index('\nstruct Frame {',begin)
    text=text[:begin]+'inline std::uint64_t HostCommand() { return evac_latency::CurrentTestCommand(); }\n'+text[end:]
    text=replace_one(text,'  std::mutex mutex;',
        '  std::mutex mutex;\n  vk::Device device;\n  vk::Semaphore last_semaphore;\n  std::uint64_t last_tick{};')
    text=replace_one(text,'    if (!buffer) {','    device = instance.GetDevice();\n    if (!buffer) {')
    text=replace_one(text,'  void Ready(unsigned token, std::uint64_t ready) {',
        '  void Ready(unsigned token, std::uint64_t ready, vk::Semaphore semaphore, std::uint64_t tick) {')
    text=replace_one(text,'    if (token > count || closed || !frames[token - 1].completed || !ready) { failed = true; return; }',
        '    if (token > count || closed || !ready || !tick || (last_semaphore && last_semaphore != semaphore) || tick < last_tick) { failed = true; return; }\n'
        '    last_semaphore = semaphore; last_tick = tick;')
    text=replace_one(text,'    unsigned valid{};\n    for (; valid < count && frames[valid].completed; ++valid) {}',
        '''    // Only teardown waits. Freeze new reservations, then wait on the
    // original draw timeline after its last recorded copy submission. A token
    // with no Ready record is NOT promoted to completed by this wait.
    if (last_tick) {
      const auto status = device.waitSemaphores({.semaphoreCount = 1,
          .pSemaphores = &last_semaphore, .pValues = &last_tick}, 5'000'000'000ull);
      if (status != vk::Result::eSuccess) failed = true;
      else for (unsigned i = 0; i < count; ++i) frames[i].completed = frames[i].ready != 0;
    }
    unsigned valid{};
    for (; valid < count && frames[valid].completed; ++valid) {}''')
    # Each game frame may also be reused by shad's host UI; record only its
    # first actual game Present, never silently overwrite its original identity.
    text=replace_one(text,'  static bool Write(HANDLE file, const void* bytes, std::size_t size) {',
        '''  void PresentedReady(std::uint64_t ready, std::uint64_t begin, std::uint64_t end, std::uint32_t result) {
    if (!configured || !ready) return;
    std::lock_guard lock(mutex);
    for (unsigned i=0; i<count; ++i) if (frames[i].ready == ready) {
      if (!frames[i].present_begin) {
        frames[i].present_begin=begin; frames[i].present_end=end; frames[i].result=result;
      }
      return;
    }
  }
  static bool Write(HANDLE file, const void* bytes, std::size_t size) {''')
    text=replace_one(text,'} // namespace evac_vk_pixels',
        '''inline Capture& Pixels() {
  static auto* value = new Capture;
  static const bool registered = []() {
    std::at_quick_exit([]() { Pixels().Flush(); }); return true;
  }();
  (void)registered;
  return *value;
}
} // namespace evac_vk_pixels''')
    return text


def core_header(shared,got):
    start=shared.index('namespace ketsui_core_probe {')
    end=shared.index('\ninline void Install(Runtime& runtime) {')
    body=shared[start:end].replace('KT_SYSV','PS4_SYSV_ABI')
    a=body.index('inline bool Matches(')
    b=body.index('\nstd::uintptr_t PS4_SYSV_ABI FrameProbe(',a)
    body=body[:a]+body[b:]
    # Source hashes and original GOT/slot offsets are verified by preparation
    # and launch. Runtime target identity is additionally checked against the
    # actually loaded provider's base, not a hardcoded mapping address.
    return '#pragma once\n#include "ps4_probe.h"\n#include <cstring>\n'+body+f'''
inline std::uint64_t WrapImport(Core::Module* module, std::uint64_t address,
    std::uint64_t target, Core::Module* provider) {{
  if (!evac_latency::state || module->name != "ketsui_01000.prx" ||
      address != module->GetBaseAddress()+{got}) return target;
  if (!provider || provider->name != "emu_integration_01400.prx" ||
      target != provider->GetBaseAddress()+0x5c0a0)
    throw std::runtime_error("Private shad Ketsui original input provider differs");
  original_apply = reinterpret_cast<Apply>(target);
  return reinterpret_cast<std::uintptr_t>(&ApplyProbe);
}}
inline void Install(Core::Module* module) {{
  if (!evac_latency::state) return;
  if (module->name == "eboot.bin") Point(98,0x29629,module->GetBaseAddress());
  if (module->name != "ketsui_01000.prx") return;
  auto* slot = reinterpret_cast<std::uintptr_t*>(module->GetBaseAddress()+0x50088);
  if (*slot == reinterpret_cast<std::uintptr_t>(&FrameProbe)) return;
  const auto expected = module->GetBaseAddress()+0x6610;
  if (*slot != expected) throw std::runtime_error("Private shad Ketsui frame slot differs");
  original_frame = reinterpret_cast<Frame>(expected);
  *slot = reinterpret_cast<std::uintptr_t>(&FrameProbe);
  Point(99,0x50088,module->GetBaseAddress(),expected,*slot);
}}
}} // namespace ketsui_core_probe
'''


def instrument_presenter(text):
    text=replace_one(text,'#include "ps4_probe.h"',
        '#include "ps4_probe.h"\n#include "shad_vulkan_pixel_probe.h"')
    text=replace_one(text,'    flip_scheduler.Finish();',
        '    flip_scheduler.Finish();\n    evac_vk_pixels::Pixels().Flush();')
    text=replace_one(text,'    // Continue with host-side passes that draw the displayed (scaled) frame.',
        '''    // Private ROI of the same guest image used by this Frame. Do not
    // use a later host UI frame as its input-response identity.
    runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    const auto pixel_token = evac_vk_pixels::Pixels().Prepare(instance, cmdbuf,
        image.GetImage(), image_size, view_info.format, probe_prepare,
        evac_latency::CurrentTestCommand());

    // Continue with host-side passes that draw the displayed (scaled) frame.''')
    text=replace_one(text,'reinterpret_cast<uintptr_t>(static_cast<VkImage>(frame->image)), frame->ready_tick, 1);',
        'reinterpret_cast<uintptr_t>(static_cast<VkImage>(frame->image)), frame->ready_tick, 1);\n'
        '    evac_vk_pixels::Pixels().Ready(pixel_token,frame->latency_ready,frame->ready_semaphore,frame->ready_tick);')
    return text


def instrument_linker(text,got):
    text=replace_one(text,'namespace Core {',
        '#include "shad_ketsui_core_probe.h"\n\nnamespace Core {')
    text=replace_one(text,'        if (rel_is_resolved) {\n            std::memcpy(',
        f'''        if (rel_is_resolved && module->name == "ketsui_01000.prx" &&
            rel_virtual_addr == module->GetBaseAddress()+{got}) {{
            Module* provider = nullptr;
            for (const auto& item : m_modules)
                if (item->name == "emu_integration_01400.prx" &&
                    rel_value == item->GetBaseAddress()+0x5c0a0) provider = item.get();
            rel_value = ketsui_core_probe::WrapImport(module,rel_virtual_addr,rel_value,provider);
        }}
        if (rel_is_resolved) {{
            std::memcpy(''')
    text=replace_one(text,'    });\n}\n\nbool Linker::Resolve(',
        '    });\n    ketsui_core_probe::Install(module);\n}\n\nbool Linker::Resolve(')
    return text


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    source=base/'shadps4-source';destination=args.destination.resolve()
    if not destination.is_relative_to(base) or destination.exists(): raise ValueError('Fresh private output required')
    audit_path=base/'ketsui-core-flow-r2/report.json';audit=json.loads(audit_path.read_text())
    for item in audit['sources']:
        if digest(Path(item['path']))!=item['sha256']: raise ValueError('Original audited game changed')
    got=audit['imports']['deliver_input']['got']
    texts={}
    for name,relative in (('linker.cpp','src/core/linker.cpp'),
        ('vk_presenter.cpp','src/video_core/renderer_vulkan/vk_presenter.cpp'),
        ('vk_swapchain.cpp','src/video_core/renderer_vulkan/vk_swapchain.cpp')):
        path=source/relative;text=path.read_text(encoding='utf-8')
        if name=='linker.cpp': text=instrument_linker(text,got)
        elif name=='vk_presenter.cpp': text=instrument_presenter(text)
        else:
            text=replace_one(text,'#include "ps4_probe.h"',
                '#include "ps4_probe.h"\n#include "shad_vulkan_pixel_probe.h"')
            text=replace_one(text,'    evac_latency::Record(evac_latency::Kind::Ps4PresentEnd, probe_begin,',
                '    const auto probe_end = evac_latency::Record(evac_latency::Kind::Ps4PresentEnd, probe_begin,')
            text=replace_one(text,'        evac_latency::ps4_present.ready, static_cast<u32>(result));',
                '        evac_latency::ps4_present.ready, static_cast<u32>(result));\n'
                '    if (evac_latency::ps4_present.game && !evac_latency::ps4_present.reuse)\n'
                '        evac_vk_pixels::Pixels().PresentedReady(evac_latency::ps4_present.ready,probe_begin,probe_end,static_cast<u32>(result));')
        texts[name]=(path,text)
    common=ROOT/'tools/latency/ps4_ketsui_core_probe.h'
    pixels=ROOT/'tools/latency/vulkan_pixel_probe.h'
    texts['shad_ketsui_core_probe.h']=(common,core_header(common.read_text(),got))
    texts['shad_vulkan_pixel_probe.h']=(pixels,pixel_header(pixels.read_text()))
    destination.mkdir();files=[]
    for name,(path,text) in texts.items():
        output=destination/name;output.write_text(text,encoding='utf-8')
        files.append(dict(source=str(path),original_sha256=digest(path),copy=str(output),copy_sha256=digest(output)))
    write_json_new(destination/'preparation.json',dict(files=files,audit_sha256=digest(audit_path),
        private_only=True,guest_instruction_rewrite=False,added_gameplay_waits=False,
        pixel_ready_is_submission_not_GPU_completion=True,generator_sha256=digest(Path(__file__))))
    print(destination)


if __name__=='__main__': main()
