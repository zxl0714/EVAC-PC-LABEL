"""Keep the ROM-free issue #1 mechanisms wired into the actual launch paths."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CompatibilityWiringTests(unittest.TestCase):
    def text(self, path):
        return (ROOT / path).read_text(encoding='utf-8')

    def test_both_graphics_paths_use_shared_extension_negotiation(self):
        native = self.text('backends/ps4/graphics/vulkan_device.cpp')
        gpu = self.text('third_party/ps4/gpu_core/upstream/src/video_core/renderer_vulkan/vk_instance.cpp')
        platform = self.text('third_party/ps4/gpu_core/upstream/src/video_core/renderer_vulkan/vk_platform.cpp')
        self.assertIn('presentation::SurfaceExtensions', native)
        self.assertIn('presentation::SurfaceExtensions', platform)
        for source in (native, gpu):
            self.assertIn('presentation::SwapchainExtension', source)
        self.assertIn('&maintenance_surfaces', gpu)

    def test_old_device_api_is_a_recoverable_error_before_format_queries(self):
        gpu = self.text('third_party/ps4/gpu_core/upstream/src/video_core/renderer_vulkan/vk_instance.cpp')
        constructor = gpu.split('Instance::Instance(Frontend::WindowSDL&', 1)[1].split('Instance::~Instance()', 1)[0]
        self.assertIn('RequireVulkanApi(properties.apiVersion, TargetVulkanApiVersion', constructor)
        self.assertLess(constructor.index('RequireVulkanApi('), constructor.index('GetFormatProperties(physical_device)'))
        self.assertLess(constructor.index('RequireVulkanApi('), constructor.index('CreateDevice();'))
        self.assertNotIn('ASSERT_MSG(properties.apiVersion', constructor)
        guard = self.text('third_party/ps4/gpu_core/host/runtime_api.cpp').split('bool Guard(', 1)[1].split('bool Create(', 1)[0]
        self.assertIn('catch (const std::exception &error)', guard)
        self.assertIn('error.what()', guard)
        self.assertIn('return false;', guard)

    def test_verified_identity_precedes_shared_seed_decoder(self):
        source = self.text('backends/ps4/content/resource_prepare.cpp')
        key = source[source.index('ResourceBytes ArchiveKey('):source.index('ResourceBytes ReadArchiveSeed(')]
        self.assertLess(key.index('Require('), key.index('ReadArchiveSeed('))
        seed = source[source.index('ResourceBytes ReadArchiveSeed('):source.index('ResourceBytes ArchiveMask(')]
        self.assertIn('PlainSelfElf(source)', seed)

    def test_preparation_and_launch_share_tested_version_policy(self):
        prepare = self.text('backends/ps4/loader/prepare_game.cpp')
        self.assertIn('PlanPs4Data(game,inspected.version,inspected.layout)', prepare)
        self.assertIn('{"communityOffline",policy.community_offline}', prepare)
        self.assertIn('if (policy.read_fixes)', prepare)
        self.assertIn('AppendVersionArguments', self.text('backends/ps4/collection_launch.cpp'))
        self.assertIn('CommunityEnabledForData(config.enabled,community_offline!=nullptr)', self.text('backends/ps4/native_probe.cpp'))
        self.assertIn('ps4_compatibility', self.text('scripts/Test.ps1'))

    def test_feveron_cache_and_explicit_preparation_use_own_replay_fix(self):
        for path in ('backends/ps4/loader/prepare_game.cpp',
                     'backends/ps4/loader/prepare_cli.cpp'):
            source = self.text(path)
            self.assertIn('game=="feveron"?PrepareFeveronReadFixes(data)', source)
        policy = self.text('backends/ps4/loader/preparation_policy.h')
        self.assertIn('feveron', policy)

if __name__ == '__main__':
    unittest.main()
