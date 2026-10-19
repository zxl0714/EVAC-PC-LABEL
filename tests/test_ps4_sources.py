import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ps4_sources as module


class Ps4SourceTests(unittest.TestCase):
    def fixture(self, root):
        seed = root / 'cache/Fixture'
        seed.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(seed)], check=True)
        (seed / 'source.cpp').write_text('// authored\n', encoding='utf-8')
        subprocess.run(['git', '-C', str(seed), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(seed), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        item = dict(name='Fixture', revision=module.git(seed, 'rev-parse', 'HEAD'),
                    url='https://github.com/fixture/fixture.git')
        module.git(seed, 'remote', 'add', 'origin', item['url'])
        collection = root / 'collection'
        (collection / 'tools').mkdir(parents=True)
        (collection / 'tools/ps4-source.lock.json').write_text(json.dumps(dict(format=1, dependencies=[item])))
        return collection, seed, item

    def test_cache_restore_is_independent_and_dirty_targets_are_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            collection, seed, item = self.fixture(root)
            result = module.restore(collection, seed.parent)
            self.assertEqual(result['verifiedDependencies'], 1)
            target = collection / '.deps/ps4/Fixture'
            module.verify_checkout(target, item)
            self.assertFalse((target / '.git/objects/info/alternates').exists())
            (seed / 'source.cpp').write_text('old project changed', encoding='utf-8')
            self.assertEqual((target / 'source.cpp').read_text(), '// authored\n')
            self.assertEqual(module.restore(collection, verify_only=True), result)
            (target / 'source.cpp').write_text('preserve', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'dirty'):
                module.restore(collection)
            self.assertEqual((target / 'source.cpp').read_text(), 'preserve')

    def test_lock_rejects_paths_duplicates_unpinned_and_non_github_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            collection, seed, item = self.fixture(Path(temp))
            path = collection / 'tools/ps4-source.lock.json'
            for changes in ({'name': '../escape'}, {'name': 'X:Y'}, {'revision': 'main'},
                            {'url': 'file:///old/project'}, {'url': 'https://github.com/a/b.git\n--bad'}):
                invalid = dict(item, **changes)
                path.write_text(json.dumps(dict(format=1, dependencies=[invalid])))
                with self.assertRaises(ValueError):
                    module.locked_dependencies(collection)
            path.write_text(json.dumps(dict(format=1, dependencies=[item, dict(item, name='fixture')])))
            with self.assertRaises(ValueError):
                module.locked_dependencies(collection)
            self.assertFalse((collection / '.deps').exists())

    def test_missing_verify_and_foreign_origin_do_not_mutate(self):
        with tempfile.TemporaryDirectory() as temp:
            collection, seed, item = self.fixture(Path(temp))
            with self.assertRaises(FileNotFoundError):
                module.restore(collection, verify_only=True)
            module.git(seed, 'remote', 'set-url', 'origin', 'https://github.com/fixture/other.git')
            with self.assertRaisesRegex(ValueError, 'differs'):
                module.restore(collection, seed.parent)
            self.assertFalse((collection / '.deps').exists())

    def test_current_lock_matches_maintained_component_declarations(self):
        root = Path(__file__).resolve().parents[1]
        locked = {item['name']: item for item in module.locked_dependencies(root)}
        self.assertEqual(len(locked), 19)
        declared = {}
        for name in ('gcn', 'gpu_core', 'input', 'audio', 'community'):
            for raw in json.loads((root / f'third_party/ps4/{name}/dependencies.json').read_text()):
                item = {key: raw[key] for key in ('name', 'revision', 'url')}
                self.assertNotIn(item['name'], declared)
                declared[item['name']] = item
        self.assertEqual({name: item for name, item in locked.items() if name != 'Vulkan-Headers'}, declared)


if __name__ == '__main__':
    unittest.main()
