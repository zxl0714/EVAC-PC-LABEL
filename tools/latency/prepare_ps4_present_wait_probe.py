"""Add an asynchronous completion observer only to an existing private probe."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from project import digest, write_json_new
from prepare_ps4_native_probe import replace_one


def prepare(fixture, destination):
    private = ROOT/'.local/input-latency-v080-r1'
    if not fixture.is_relative_to(private) or not destination.is_relative_to(private):
        raise ValueError('Private source and fresh destination required')
    original = json.loads((fixture/'preparation.json').read_text())
    for item in original['files']:
        if digest(Path(item['source'])) != item['original_sha256'] or digest(Path(item['copy'])) != item['copy_sha256']:
            raise ValueError('Input probe source identity changed')
    destination.mkdir(exist_ok=False)
    for source in fixture.iterdir():
        if source.is_file() and source.suffix in ('.cpp', '.h'):
            shutil.copy2(source, destination/source.name)
    items = [{**item, 'copy':str(destination/Path(item['copy']).name)} for item in original['files']]
    def save(source, text):
        copy = destination/source.name
        copy.write_text(text, encoding='utf-8')
        existing = [i for i in items if i['source'] == str(source)]
        if existing:
            existing[0].update(copy=str(copy), copy_sha256=digest(copy))
        else:
            items.append(dict(source=str(source), original_sha256=digest(source),
                              copy=str(copy), copy_sha256=digest(copy)))

    source = ROOT/'third_party/ps4/gpu_core/host/presenter.cpp'
    text = (destination/'presenter.cpp').read_text()
    text = replace_one(text, '#include <utility>', '#include <utility>\n#include "vulkan_present_wait.h"')
    text = replace_one(text, '  vk::UniqueSwapchainKHR chain;\n  vk::PresentModeKHR latency_present_mode{};', '''  vk::UniqueSwapchainKHR chain;
  vk::PresentModeKHR latency_present_mode{};
  std::unique_ptr<evac_latency::VulkanPresentWait> latency_wait;
  unsigned latency_chain_generation{};''')
    text = replace_one(text, '    views.clear();\n    chain.reset();',
                       '    latency_wait.reset();\n    views.clear();\n    chain.reset();')
    text = replace_one(text, '  void RetireChain() {',
                       '  void RetireChain() {\n    latency_wait.reset(); // before retirement, not just destruction')
    text = replace_one(text, '    images =\n        Checked(device.getSwapchainImagesKHR(*chain), "Get swapchain images");', '''    wchar_t latency_path[32768]{};
    const auto latency_length=GetEnvironmentVariableW(L"EVAC_LATENCY_TRACE",latency_path,32768);
    Require(latency_length && latency_length<32768,"Private trace output required for present wait");
    const auto wait_path=std::filesystem::path(std::wstring(latency_path)+L".wsi-"+
        std::to_wstring(++latency_chain_generation)+L"-wait.jsonl");
    latency_wait=std::make_unique<evac_latency::VulkanPresentWait>(device,*chain,wait_path);
    images =
        Checked(device.getSwapchainImagesKHR(*chain), "Get swapchain images");''')
    text = replace_one(text, '        status = output_queue.presentKHR({.pNext = instance.HasPresentFences() ? &completion : nullptr,', '''        const auto latency_wait_extra=latency_wait->Before(instance.HasPresentFences()?&completion:nullptr);
        LARGE_INTEGER latency_wait_begin{};QueryPerformanceCounter(&latency_wait_begin);
        status = output_queue.presentKHR({.pNext = latency_wait_extra,''')
    text = replace_one(text, '        const auto probe_end = Ps4ProbeHostRecord(23, probe_begin, snapshot.latency_ready, static_cast<unsigned>(status));', '''        LARGE_INTEGER latency_wait_end{};QueryPerformanceCounter(&latency_wait_end);
        latency_wait->After(probe_begin,latency_wait_begin.QuadPart,latency_wait_end.QuadPart,status);
        const auto probe_end = Ps4ProbeHostRecord(23, probe_begin, snapshot.latency_ready, static_cast<unsigned>(status));''')
    save(source, text)

    source = ROOT/'third_party/ps4/gpu_core/upstream/src/video_core/renderer_vulkan/vk_instance.cpp'
    text = source.read_text()
    text = replace_one(text, '    // Required\n', '''    // Private observer only; production source is unchanged.
    const auto wait_features=physical_device.getFeatures2<vk::PhysicalDeviceFeatures2,
        vk::PhysicalDevicePresentIdFeaturesKHR,vk::PhysicalDevicePresentWaitFeaturesKHR>();
    if (!wait_features.get<vk::PhysicalDevicePresentIdFeaturesKHR>().presentId ||
        !wait_features.get<vk::PhysicalDevicePresentWaitFeaturesKHR>().presentWait ||
        !add_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME) ||
        !add_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) return false;
    // Required
''')
    text = replace_one(text, '    auto [device_result, dev] = physical_device.createDeviceUnique(device_chain.get());', '''    vk::PhysicalDevicePresentWaitFeaturesKHR private_wait{
        .pNext=const_cast<void*>(device_chain.get().pNext),.presentWait=true};
    vk::PhysicalDevicePresentIdFeaturesKHR private_id{.pNext=&private_wait,.presentId=true};
    device_chain.get().pNext=&private_id;
    auto [device_result, dev] = physical_device.createDeviceUnique(device_chain.get());''')
    save(source, text)
    source = ROOT/'tools/latency/vulkan_present_wait.h'
    save(source, source.read_text())
    write_json_new(destination/'preparation.json', dict(files=items, private_only=True,
        present_wait_observer=True, base_preparation_sha256=digest(fixture/'preparation.json'),
        generator_sha256=digest(Path(__file__))))
    print(destination)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args=parser.parse_args()
    prepare(args.fixture.resolve(strict=True), args.destination.resolve())
