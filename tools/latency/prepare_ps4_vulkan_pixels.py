"""Add owned-snapshot Vulkan ROI copies to verified private native source copies."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from prepare_ps4_native_probe import replace_one


def instrument_host(text):
    return replace_one(text, 'extern "C" __declspec(dllexport) std::uint64_t EvacLatencyRecord(',
        'extern "C" __declspec(dllexport) std::uint64_t EvacLatencyTestCommand() {\n'
        '    return evac_latency::CurrentTestCommand();\n}\n'
        'extern "C" __declspec(dllexport) std::uint64_t EvacLatencyRecord(')


def instrument_presenter(text):
    text = replace_one(text, '#include "ps4_probe_exports.h"',
        '#include "ps4_probe_exports.h"\n#include "vulkan_pixel_probe.h"')
    text = replace_one(text, '    u64 latency_prepare{}, latency_ready{}, latency_address{};',
        '    u64 latency_prepare{}, latency_ready{}, latency_address{};\n    unsigned latency_pixel_token{};')
    text = replace_one(text, '  std::array<Snapshot, PresentSnapshotSlots> snapshots;',
        '  evac_vk_pixels::Capture latency_pixels;\n  std::array<Snapshot, PresentSnapshotSlots> snapshots;')
    # Drain has finished and the presentation worker is joined before any CPU
    # mapping / IO. Match this destructor only, not the resize clear path.
    text = replace_one(text, '    views.clear();\n    chain.reset();\n  }\n  void Retire(',
        '    latency_pixels.Flush();\n    views.clear();\n    chain.reset();\n  }\n  void Retire(')
    text = replace_one(text, '                      vk::ImageLayout::eTransferDstOptimal, copy);',
        '                      vk::ImageLayout::eTransferDstOptimal, copy);\n'
        '    frame.latency_pixel_token = latency_pixels.Prepare(instance, command, image.GetImage(),\n'
        '        size, source_format, frame.latency_prepare, evac_vk_pixels::HostCommand());')
    text = replace_one(text, '    RecordSnapshot(frame, display, desc);',
        '    frame.latency_prepare = probe_prepare;\n    RecordSnapshot(frame, display, desc);')
    text = replace_one(text, '    frame.copy_pending = false;\n    result.guest_copy_completed = true;',
        '    frame.copy_pending = false;\n    latency_pixels.Completed(frame.latency_pixel_token);\n'
        '    result.guest_copy_completed = true;')
    text = replace_one(text, 'reinterpret_cast<std::uintptr_t>(static_cast<VkImage>(frame.snapshot->image)), frame.copy_tick, 1);',
        'reinterpret_cast<std::uintptr_t>(static_cast<VkImage>(frame.snapshot->image)), frame.copy_tick, 1);\n'
        '      latency_pixels.Ready(frame.latency_pixel_token, frame.latency_ready);')
    text = replace_one(text,
        '        Ps4ProbeHostRecord(23, probe_begin, snapshot.latency_ready, static_cast<unsigned>(status));',
        '        const auto probe_end = Ps4ProbeHostRecord(23, probe_begin, snapshot.latency_ready, static_cast<unsigned>(status));\n'
        '        latency_pixels.Presented(snapshot.latency_pixel_token, probe_begin, probe_end, static_cast<unsigned>(status));')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    base, destination = args.base.resolve(strict=True), args.destination.resolve()
    private = ROOT / '.local/input-latency-v080-r1'
    if not base.is_relative_to(private) or not destination.is_relative_to(private) or destination.exists():
        raise ValueError('Verified private base and fresh output required')
    prior = json.loads((base / 'preparation.json').read_text())
    for item in prior['files']:
        if digest(Path(item['source'])) != item['original_sha256'] or digest(Path(item['copy'])) != item['copy_sha256']:
            raise ValueError('Original or private source identity changed')
    texts = {Path(item['copy']).name: Path(item['copy']).read_text(encoding='utf-8') for item in prior['files']}
    texts['presenter.cpp'] = instrument_presenter(texts['presenter.cpp'])
    texts['native_probe.cpp'] = instrument_host(texts['native_probe.cpp'])
    destination.mkdir()
    files = []
    for item in prior['files']:
        output = destination / Path(item['copy']).name
        output.write_text(texts[output.name], encoding='utf-8')
        files.append(dict(item, copy=str(output), copy_sha256=digest(output)))
    header = ROOT / 'tools/latency/vulkan_pixel_probe.h'
    shutil.copy2(header, destination / header.name)
    files.append(dict(source=str(header), original_sha256=digest(header),
                      copy=str(destination / header.name), copy_sha256=digest(destination / header.name)))
    write_json_new(destination / 'preparation.json', dict(files=files, private_only=True,
        base_preparation_sha256=digest(base / 'preparation.json'),
        generator_sha256=digest(Path(__file__)), added_per_frame_waits=False,
        pixel_copy_has_cost=True, original_code_instructions_modified=False))
    print(destination)


if __name__ == '__main__': main()
