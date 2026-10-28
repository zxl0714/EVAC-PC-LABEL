"""Generate private host/GPU source copies for a PS4 latency measurement build."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new

DEST = ROOT / '.local/input-latency-v080-r1/evac-ps4-probe-r7'


def replace_one(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Unexpected source layout: ' + before[:90])
    return text.replace(before, after, 1)


def prepare():
    DEST.mkdir(exist_ok=False)
    copied = []

    def save(relative, content):
        source = ROOT / relative
        destination = DEST / source.name
        destination.write_text(content, encoding='utf-8')
        copied.append(dict(source=str(source), original_sha256=digest(source),
                           copy=str(destination), copy_sha256=digest(destination)))

    relative = 'backends/ps4/native_probe.cpp'
    text = (ROOT / relative).read_text(encoding='utf-8')
    # Preserve every existing guest shutdown decision and code. Only flush the
    # bounded measurement trace before the original process termination call.
    text = text.replace('ExitProcess(', 'LatencyExitProcess(')
    text = replace_one(text, '    TerminateProcess(GetCurrentProcess(), 27);',
        '    evac_latency::Flush();\n    TerminateProcess(GetCurrentProcess(), 27);')
    text = '''#include "ps4_probe.h"
extern "C" __declspec(dllexport) std::uint64_t EvacLatencyRecord(std::uint32_t kind,
    std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
    std::uint64_t e, std::uint64_t f) {
    return evac_latency::Record(static_cast<evac_latency::Kind>(kind), a,b,c,d,e,f);
}
[[noreturn]] static void LatencyExitProcess(UINT status) {
    evac_latency::Flush(); ::ExitProcess(status);
}
''' + text
    text = replace_one(text, 'int wmain(int argc,wchar_t** argv) {',
        '''int wmain(int argc,wchar_t** argv) {
    evac_latency::Initialize();
    evac_latency::InitializeTestPad();''')
    save(relative, text)

    relative = 'backends/ps4/native_pad.cpp'
    text = '#include "ps4_probe.h"\n#include "ps4_test_latch.h"\n' + (ROOT / relative).read_text(encoding='utf-8')
    text = replace_one(text, '    bool synthetic_input{};', '''    bool synthetic_input{};
    evac_latency::Ps4TestLatch probe_latch;
    void RefreshProbe() {
        if (!evac_latency::test_pad) return;
        const auto sample = evac_latency::ReadTestPad();
        probe_latch.Update(sample.event, evac_latency::Ps4Buttons(sample.buttons));
    }
    struct ProbeSource { std::uint64_t timestamp{}, source{}; std::uint32_t buttons{}; };
    std::array<ProbeSource, 128> probe_sources{};
    unsigned probe_next{};
    std::uint64_t ProbeSourceFor(const PadData& report) const {
        for (unsigned back=0; back<probe_sources.size(); ++back) {
            const auto& source = probe_sources[(probe_next + probe_sources.size() - 1 - back) % probe_sources.size()];
            if (source.timestamp == report.timestamp && source.buttons == report.buttons) return source.source;
        }
        return 0;
    }''')
    text = replace_one(text, '        history.Push(output);', '''        if (evac_latency::test_pad) {
            RefreshProbe();
            output.buttons = probe_latch.Buttons();
            probe_sources[probe_next] = {output.timestamp, probe_latch.Source(), output.buttons};
            probe_next = (probe_next + 1) % probe_sources.size();
        }
        history.Push(output);''')
    text = replace_one(text,
        '    const bool edge = state.keyboard.pending != 0 || state.gamepad.HasPending();',
        '''    state.RefreshProbe();
    const bool edge = state.keyboard.pending != 0 || state.gamepad.HasPending() || state.probe_latch.pending;''')
    text = replace_one(text,
        '    const auto pending=state.keyboard.pending | state.physical.pending;',
        '    const auto pending=state.keyboard.pending | state.physical.pending | state.probe_latch.pending;')
    text = replace_one(text, '        state.gamepad.Acknowledge(state.physical);',
        '        state.gamepad.Acknowledge(state.physical);\n        state.probe_latch.Acknowledge();')
    text = replace_one(text, 'int Read(int handle, PadData* output, int count, bool latest) {',
        'int Read(int handle, PadData* output, int count, bool latest, const void* caller = nullptr) {')
    text = replace_one(text, 'return Read(handle, output, count, false);',
        'return Read(handle, output, count, false, __builtin_return_address(0));')
    text = replace_one(text, 'return Read(handle, output, 1, true);',
        'return Read(handle, output, 1, true, __builtin_return_address(0));')
    text = replace_one(text, '    return ReadReports(state, output, count, latest);', '''
    const auto probe_begin = evac_latency::Record(evac_latency::Kind::Ps4PadBegin,
        static_cast<unsigned>(handle), count, reinterpret_cast<std::uintptr_t>(output),
        reinterpret_cast<std::uintptr_t>(caller), latest);
    const auto result = ReadReports(state, output, count, latest);
    const int reports = latest && result == 0 ? 1 : result;
    for (int i=0; i<reports; ++i)
        evac_latency::Record(evac_latency::Kind::Ps4PadReport, probe_begin, i,
            output[i].buttons, output[i].timestamp, output[i].connected,
            evac_latency::test_pad ? state.ProbeSourceFor(output[i]) : 0);
    evac_latency::Record(evac_latency::Kind::Ps4PadEnd, probe_begin,
        static_cast<unsigned>(result), reports);
    return result;''')
    text = replace_one(text, 'void NativePadWindowKey(void* window, unsigned key, bool down) {',
        '''void NativePadWindowKey(void* window, unsigned key, bool down) {
    evac_latency::Record(evac_latency::Kind::WindowKey, key, down,
        reinterpret_cast<std::uintptr_t>(window));''')
    save(relative, text)

    relative = 'backends/ps4/input/sdl_gamepad.cpp'
    text = '#include "ps4_probe.h"\n' + (ROOT / relative).read_text(encoding='utf-8')
    text = replace_one(text, '    static bool SDLCALL Watch(void* context, SDL_Event* event) {', '''
    static bool SDLCALL Watch(void* context, SDL_Event* event) {
        if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event->type == SDL_EVENT_GAMEPAD_BUTTON_UP)
            evac_latency::Record(evac_latency::Kind::Ps4SdlEvent, event->type,
                event->gbutton.button, event->gbutton.down, event->common.timestamp,
                SDL_GetTicksNS(), event->gbutton.which);''')
    save(relative, text)

    relative = 'third_party/ps4/gpu_core/host/presenter.cpp'
    text = '#include "ps4_probe_exports.h"\n' + (ROOT / relative).read_text(encoding='utf-8')
    text = replace_one(text, '    u64 copy_tick{}, sequence{};',
        '    u64 copy_tick{}, sequence{};\n    u64 latency_prepare{}, latency_ready{}, latency_address{};')
    text = replace_one(text, '  vk::UniqueSwapchainKHR chain;\n  vk::SwapchainKHR replacement_old{};',
        '  vk::UniqueSwapchainKHR chain;\n  vk::PresentModeKHR latency_present_mode{};\n  vk::SwapchainKHR replacement_old{};')
    text = replace_one(text, '.presentMode = SelectMode(),',
        '.presentMode = (latency_present_mode = SelectMode()),')
    text = replace_one(text, '  RuntimePresentResult Prepare(const RuntimeDisplay &display) {',
        '''  RuntimePresentResult Prepare(const RuntimeDisplay &display) {
    const auto probe_prepare = Ps4ProbeHostRecord(26, display.address, display.width, display.height);''')
    text = replace_one(text, '    frame.copy_pending = true;', '''    frame.copy_pending = true;
    frame.latency_prepare = probe_prepare;
    frame.latency_ready = 0;
    frame.latency_address = display.address;''')
    text = replace_one(text, '      frame.sequence = ++presents;', '''      frame.sequence = ++presents;
      frame.latency_ready = Ps4ProbeHostRecord(21, frame.latency_prepare,
          reinterpret_cast<std::uintptr_t>(&frame), frame.latency_address,
          reinterpret_cast<std::uintptr_t>(static_cast<VkImage>(frame.snapshot->image)), frame.copy_tick, 1);''')
    text = replace_one(text, '        status = output_queue.presentKHR(', '''        const auto probe_begin = Ps4ProbeHostRecord(22, snapshot.latency_ready,
            reinterpret_cast<std::uintptr_t>(static_cast<VkSwapchainKHR>(handle)), index,
            static_cast<unsigned>(latency_present_mode), 1, 0);
        status = output_queue.presentKHR(''')
    text = replace_one(text, '                                          .pImageIndices = &index});',
        '''                                          .pImageIndices = &index});
        Ps4ProbeHostRecord(23, probe_begin, snapshot.latency_ready, static_cast<unsigned>(status));''')
    save(relative, text)
    write_json_new(DEST / 'preparation.json', dict(files=copied, private_only=True,
        generator_sha256=digest(Path(__file__))))
    print(DEST)


if __name__ == '__main__': prepare()
