"""Generate the one private D3D12 presenter copy; do not alter production SDK."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new


def replace(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Pixel injection anchor changed: ' + before[:90])
    return text.replace(before, after)


def instrument(text, native):
    text = '#include "d3d12_pixel_probe.h"\n' + text
    if not native:
        text = '#include "test_pad.h"\n' + text
    # Flush only after original teardown synchronization, never inside gameplay.
    wait = ('  paint_context_.AwaitSwapChainUsageCompletion();' if native else
            '  paint_context_.AwaitSwapChainUsageCompletion();')
    # Match destructor context so other swapchain resize waits remain untouched.
    start = text.index('D3D12Presenter::~D3D12Presenter() {')
    end = text.index('\n}', start)
    destructor = text[start:end]
    if wait not in destructor:
        raise ValueError('Original destructor synchronization changed')
    text = text[:start] + replace(destructor, wait, wait + '\n  evac_pixels::Flush();') + text[end:]
    command = 'evac_pixels::HostCommand()' if native else 'evac_latency::CurrentTestCommand()'
    keys = ('guest_output_properties.diagnostic_frame, guest_output_properties.diagnostic_packet, current_paint_submission'
            if native else 'current_paint_submission, back_buffer_index, guest_output_properties.latency_ready')
    drawn = 'diagnostic_guest_drawn' if native else 'latency_guest_drawn'
    text = replace(text, '  // End drawing to the back buffer.',
        ('''  static const bool pixel_exit_registered = []() {
    std::at_quick_exit(evac_pixels::Flush); return true;
  }();
  (void)pixel_exit_registered;
''' if not native else '') + f'''  const auto pixel_token = evac_pixels::Pixels().Prepare(device, command_list,
      back_buffer, {command}, {keys}, {drawn});

  // End drawing to the back buffer.''')
    execute = ('  provider_.GetDirectQueue()->ExecuteCommandLists(1, &execute_command_list);' if native else
               '  direct_queue->ExecuteCommandLists(1, &execute_command_list);')
    queue = 'provider_.GetDirectQueue()' if native else 'direct_queue'
    anchor = execute if native else execute + '\n  if (execute_ui_drawers) {'
    text = replace(text, anchor, execute + f'\n  evac_pixels::Pixels().Submitted({queue}, pixel_token);' +
                   ('' if native else '\n  if (execute_ui_drawers) {'))
    begin = '  HRESULT present_result = paint_context_.swap_chain->Present('
    text = replace(text, begin, '  const auto pixel_begin = evac_pixels::Clock();\n' + begin)
    if native:
        after = '  const auto present_end_us = graphics::FrameTiming::NowMicros();'
        text = replace(text, after,
            '  const auto pixel_end = evac_pixels::Clock();\n' + after +
            '\n  evac_pixels::Pixels().Presented(pixel_token, pixel_begin, pixel_end, present_result);')
    else:
        after = '  evac_latency::Record(evac_latency::Kind::PresentEnd, latency_begin,'
        text = replace(text, after,
            '  const auto pixel_end = evac_pixels::Clock();\n'
            '  evac_pixels::Pixels().Presented(pixel_token, pixel_begin, pixel_end, present_result);\n' + after)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--xenia', action='store_true')
    args = parser.parse_args()
    destination = args.directory.resolve()
    private = ROOT / '.local/input-latency-v080-r1'
    if not destination.is_relative_to(private): raise ValueError('Private destination required')
    source = ((private / 'xenia-source/src/xenia/ui/d3d12/d3d12_presenter.cc') if args.xenia else
              ROOT / 'third_party/rexglue/src/ui/d3d12/d3d12_presenter.cpp')
    # Transform and validate all anchors before creating any output.
    text = instrument(source.read_text(encoding='utf-8'), not args.xenia)
    destination.mkdir(exist_ok=False)
    output = destination / source.name
    output.write_text(text, encoding='utf-8')
    write_json_new(destination / 'preparation.json', dict(source=str(source),
        original_sha256=digest(source), copy=str(output), copy_sha256=digest(output),
        pixel_header_sha256=digest(ROOT / 'tools/latency/d3d12_pixel_probe.h')))
    print(destination)


if __name__ == '__main__': main()
