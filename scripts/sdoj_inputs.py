"""Identify local SDOJ program/TU variants without extracting, patching or running them."""
import argparse
import json
from pathlib import Path
import re

from analysis_paths import private_output
from project import ROOT, digest, reject_link, write_json_new

REFERENCE = ROOT / 'games/sdoj/input-references.json'
PROGRAMS = {'default.xex', 'CA022100.bin', 'CA022110.bin'}
PATCHES = {'default.xexp', 'CA022100.binp', 'CA022110.binp'}


def validate_reference(value):
    if (not isinstance(value, dict) or type(value.get('format')) is not int or value['format'] != 1 or
            value.get('game') != 'sdoj' or not isinstance(value.get('variants'), dict) or
            set(value['variants']) != {'disc', 'god'}):
        raise ValueError('Invalid SDOJ input references')
    for variant in value['variants'].values():
        if not isinstance(variant, dict):
            raise ValueError('Invalid SDOJ variant')
        for key, expected in (('programs', PROGRAMS), ('patches', PATCHES)):
            entries = variant.get(key, {})
            if not isinstance(entries, dict) or set(entries) != expected or any(not isinstance(h, str) or not re.fullmatch('[0-9a-f]{64}', h) for h in entries.values()):
                raise ValueError('Invalid program/patch identity table')
        updates = variant.get('updateContainers')
        if not isinstance(updates, list):
            raise ValueError('Invalid title update references')
        for update in updates:
            if (not isinstance(update, dict) or type(update.get('bytes')) is not int or update['bytes'] <= 0 or
                    not isinstance(update.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', update['sha256'])):
                raise ValueError('Invalid title update identity')
    return value


def identify(directory, reference=None):
    reference = validate_reference(reference if reference is not None else json.loads(REFERENCE.read_text(encoding='utf-8')))
    root = Path(directory).absolute()
    reject_link(root)
    if not root.is_dir():
        raise ValueError('Select an extracted local game-data directory')
    names, containers = {}, []
    for path in sorted(root.iterdir()):
        # Do not follow directory links or inspect arbitrary resource trees.
        reject_link(path)
        if not path.is_file():
            continue
        key = path.name.casefold()
        if key in names:
            raise ValueError('Ambiguous case-insensitive filename')
        names[key] = path
        if path.suffix.lower() in {'.rar', '.7z', '.zip', '.iso'}:
            containers.append(dict(path=path.name, bytes=path.stat().st_size))
    checked = {}

    def check(path):
        if path.name not in checked:
            before = path.stat()
            sha256 = digest(path)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                raise ValueError('Input changed while hashing')
            checked[path.name] = dict(path=path.name, bytes=after.st_size, sha256=sha256)
        return checked[path.name]['sha256']

    def matches(entries):
        result = {}
        for name, expected in entries.items():
            path = names.get(name.casefold())
            result[name] = 'missing' if path is None else ('matched' if check(path) == expected else 'different')
        return result

    variants = {}
    for name, variant in reference['variants'].items():
        programs, patches = matches(variant['programs']), matches(variant['patches'])
        update_matches = []
        for update in variant['updateContainers']:
            for path in names.values():
                # A multi-GB archive is never hashed just to look for a small TU.
                if path.stat().st_size == update['bytes'] and check(path) == update['sha256']:
                    update_matches.append(path.name)
        variants[name] = dict(programs=programs, patches=patches, updateContainers=update_matches,
                              programsMatch=all(state == 'matched' for state in programs.values()),
                              updateMatches=all(state == 'matched' for state in patches.values()) or bool(update_matches))
    known = [name for name, variant in variants.items() if variant['programsMatch']]
    selected = known[0] if len(known) == 1 else None
    if not selected:
        state = 'needs_extraction' if containers and not any(name.casefold() in names for name in PROGRAMS) else 'unknown_or_incomplete_programs'
    elif variants[selected]['updateMatches']:
        state = 'known_programs_and_update'
    elif any(variant['updateMatches'] for name, variant in variants.items() if name != selected):
        state = 'mismatched_update'
    else:
        state = 'missing_or_unrecognized_update'
    return dict(format=1, game='sdoj', state=state, variant=selected, readyToRun=False,
                guestExecution=False, sourceModified=False, fullDataVerification=False,
                scope='Program and TU identities only; resources, applied output and runtime remain unverified',
                source=reference.get('source'), variants=variants, filesHashed=len(checked),
                checkedFiles=sorted(checked.values(), key=lambda item: item['path']), containerCandidates=containers)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, help='New report under collection/.local')
    args = parser.parse_args()
    report = identify(args.directory)
    if args.output:
        output = private_output(args.output, args.directory)
        write_json_new(output, report)
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(json.dumps(dict(error=str(error), guestExecution=False), ensure_ascii=False))
        raise SystemExit(2)
