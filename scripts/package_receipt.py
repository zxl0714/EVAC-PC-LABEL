"""Reuse completed component builds without re-running their development checks.

Packaging checks maintained inputs and the bytes copied into the package. The
original build keeps its dependency, generation and PE/PDB validation evidence.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

from generate_catalogue import relative
from project import digest, reject_link


# These files do not contribute to player machine code. Unknown changes remain
# build inputs; don't silently bless a new build system or a new shared source.
PACKAGING = {'scripts/release_package.py', 'scripts/package_receipt.py'}
ISOLATED_PS2 = ('backends/ps2/', 'third_party/ps2/', 'tools/ps2/', 'tools/ps2-gs/',
                'scripts/ps2_', 'scripts/Build-Ps2', 'scripts/Build-Espgaluda', 'scripts/Build-Ibara', 'games/espgaluda/', 'games/ibara/')
ISOLATED_PS4 = ('backends/ps4/', 'third_party/ps4/', 'scripts/ps4/', 'scripts/ps4_',
                'scripts/Build-Ps4', 'tools/ps4', 'games/ketsui/', 'games/daioujou/',
                'games/esprade/', 'games/feveron/', 'games/garegga/')


def relevant(backend, name, files):
    if name.endswith('.md') or name.startswith(('docs/', 'tests/', 'server/')) or name in PACKAGING:
        return False
    if backend == 'xbox360':
        # Historical host certificates listed the whole repository. The release
        # takes its launcher from a separate receipt and keeps shared data metadata.
        if name.startswith('launcher/') or (
                name.startswith(ISOLATED_PS2 + ISOLATED_PS4) and not name.endswith('/data.json')):
            return False
        return True
    # Existing scoped receipts also include shared PS4 input/community sources
    # used by PS2. Their explicit entries take precedence over engine ownership.
    if name in files:
        return True
    if backend == 'ps2':
        return name.startswith(ISOLATED_PS2 + ('common/', 'backends/ps4/input/')) or name.endswith('/data.json')
    if backend == 'ps4':
        return name.startswith(ISOLATED_PS4 + ('common/',)) or name.endswith('/data.json')
    if backend == 'launcher':
        return name.startswith(('common/', 'launcher/', 'tools/ps4-extract/', 'tools/xbla-extract/')) or name.endswith('/data.json')
    raise ValueError('Unknown package component')


class PackageInputs:
    def __init__(self, root):
        self.root = Path(root).resolve()
        if self.git('status', '--porcelain', '--untracked-files=normal'):
            raise ValueError('Commit source changes before release packaging')
        self.commit = self.git('rev-parse', 'HEAD').decode().strip()
        self.hashes = {}
        self.changes = {}

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args])

    def xbox_catalogue_equivalent(self, origin, expected):
        # Ibara is PS2-only. Its availability flags do not affect any Xbox
        # executable; every other byte-level catalogue value must still match.
        try:
            old_bytes = self.git('show', origin + ':games/catalogue.json')
            if hashlib.sha256(old_bytes).hexdigest() != expected:
                return False
            before = json.loads(old_bytes)
            after = json.loads((self.root / 'games/catalogue.json').read_text(encoding='utf-8'))
            for document in (before, after):
                games = [game for game in document['games'] if game['id'] == 'ibara']
                if len(games) != 1 or games[0]['backend'] != 'ps2':
                    return False
                games[0].pop('integrated')
                games[0].pop('comingSoon')
            return before == after
        except (ValueError, KeyError, TypeError, subprocess.CalledProcessError):
            return False

    def load(self, path, backend, games=None):
        reject_link(path)
        record = json.loads(Path(path).read_text(encoding='utf-8'))
        kind = record.get('backend', record.get('kind'))
        if (type(record.get('format')) is not int or record['format'] != 1 or
                kind != ('host' if backend == 'xbox360' else backend) or
                record.get('sourceDirty') is True or
                (games is not None and (not isinstance(record.get('games'), list) or
                 (not set(games).issubset(record['games']) if backend == 'ps2'
                  else record['games'] != list(games))))):
            raise ValueError('Invalid completed build receipt: ' + backend)
        source = dict(record['sourceSnapshot'])
        claimed = source.pop('sha256')
        identity = {k:v for k,v in source.items() if not (backend == 'xbox360' and k == 'environment')}
        if hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest() != claimed:
            raise ValueError('Inconsistent build input certificate: ' + backend)
        origin = record['sourceCommit']
        if not re.fullmatch('[0-9a-f]{40}', origin):
            raise ValueError('Invalid build commit')
        # A later documentation or unrelated-engine commit can reuse this build.
        # Keep the actual origin rather than rewriting the old certificate.
        if origin not in self.changes:
            subprocess.run(['git', '-C', str(self.root), 'merge-base', '--is-ancestor', origin, self.commit], check=True)
            self.changes[origin] = [n.decode('utf-8') for n in
                self.git('diff', '--name-only', '--no-renames', '-z', origin, self.commit).split(b'\0') if n]
        files = source['files']
        if not isinstance(files, dict) or not files:
            raise ValueError('Missing maintained build inputs: ' + backend)
        equivalent_catalogue = (backend == 'xbox360' and 'games/catalogue.json' in files and
                                'games/catalogue.json' in self.changes[origin] and
                                self.xbox_catalogue_equivalent(origin, files['games/catalogue.json']))
        changed = [name for name in self.changes[origin] if relevant(backend, name, files)
                   and not (equivalent_catalogue and name == 'games/catalogue.json')]
        if changed:
            raise ValueError('Rebuild ' + backend + ': changed input ' + changed[0])
        # This catches a build made from uncommitted input, and local edits to
        # a tracked submodule dependency, without reading generated trees/PDBs.
        for name, expected in files.items():
            relative(name)
            if equivalent_catalogue and name == 'games/catalogue.json':
                continue
            if not relevant(backend, name, files):
                continue
            file = self.root / name
            if name not in self.hashes:
                reject_link(file)
                self.hashes[name] = digest(file)
            if self.hashes[name] != expected:
                raise ValueError('Rebuild ' + backend + ': source differs ' + name)
        seen = set()
        for item in record['outputs']:
            name = relative(item['path'])
            if (name in seen or (backend == 'xbox360' and '/' in name) or
                    (backend != 'xbox360' and not name.startswith('out/')) or
                    type(item.get('bytes')) is not int or item['bytes'] < 0 or
                    not re.fullmatch('[0-9a-f]{64}', item.get('sha256', ''))):
                raise ValueError('Invalid output certificate: ' + backend)
            seen.add(name)
        if not seen:
            raise ValueError('Empty output certificate: ' + backend)
        return record
