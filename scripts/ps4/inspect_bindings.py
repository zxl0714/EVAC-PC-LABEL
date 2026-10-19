"""Summarize real resolver output and optionally annotate a hash-matched call graph.

Only reads files. No game entry/initializer is executed. Static reachability is
deliberately limited to analyzer-discovered direct calls, not gameplay coverage.
"""
import argparse
import collections
import json
import struct
from pathlib import Path

from native_elf import Elf


def annotate(bindings, graph, read_code, got_symbols, roots, follow_pointers=False, image_name='eboot.bin'):
    functions = {item['offset']: item for item in graph['functions']}
    if follow_pointers and not any('function_references' in item for item in functions.values()):
        raise ValueError('Graph has no function-reference inventory; regenerate it before following callbacks')
    by_nid = {item['nid']: item for item in bindings if item['image_name'] == image_name}
    thunks = {}
    for offset in functions:
        try:
            address = int(offset, 16)
        except ValueError:
            continue
        code = read_code(address, 6)
        if len(code) != 6 or code[:2] != b'\xff\x25':
            continue
        got = address + 6 + struct.unpack_from('<i', code, 2)[0]
        record = by_nid.get(got_symbols.get(got))
        if record is not None:
            thunks[offset] = record
    callers = collections.defaultdict(set)
    for function in functions.values():
        for callee in function['callees']:
            record = thunks.get(callee['offset'])
            if record is not None:
                callers[record['nid']].add(function['offset'])
    for record in by_nid.values():
        record['static_direct_callers'] = sorted(callers[record['nid']], key=lambda value: int(value, 16))
    reachability = []
    for root in roots:
        root = hex(int(root, 16))
        if root not in functions:
            raise ValueError(f'No analyzed function at root {root}; use its entry offset')
        visited, frontier, imports = set(), [root], {}
        while frontier:
            offset = frontier.pop()
            if offset in visited:
                continue
            visited.add(offset)
            if offset in thunks:
                record = thunks[offset]
                imports[record['nid']] = {key: record[key] for key in ('nid', 'name', 'library', 'module', 'status')}
                continue
            function = functions.get(offset)
            if function is not None:
                frontier.extend(item['offset'] for item in function['callees'])
                if follow_pointers:
                    frontier.extend(item['offset'] for item in function.get('function_references', []))
        reachability.append(dict(root=root, name=functions[root]['name'], visited_nodes=len(visited),
                                 imports=sorted(imports.values(), key=lambda item: item['nid'])))
    return dict(image_name=image_name, sha256=graph['sha256'], recognized_import_thunks=len(thunks),
                discovered_function_nodes=len(functions), roots=reachability,
                follows_function_references=follow_pointers,
                scope=('Analyzer-discovered calls and possible callback function-address references; references do not prove execution'
                       if follow_pointers else 'Analyzer-discovered direct calls only; callbacks, indirect calls and unanalyzed code are not proven absent'))


def summarize(events):
    summaries = [item for item in events if item['event'] == 'native_binding_census_complete']
    if len(summaries) != 1 or events[-1] != summaries[0]:
        raise ValueError('Exactly one terminal census summary required')
    summary = summaries[0]
    if summary['game_entry_reached'] or summary['module_initializers_called'] or summary['capability_validation']:
        raise ValueError('Input is not a non-executing binding census')
    bindings = [dict(item) for item in events if item['event'] == 'native_binding_census_import']
    identities = [(item['image_name'], item['index']) for item in bindings]
    if len(set(identities)) != len(bindings):
        raise ValueError('Duplicate image/import index')
    counts = collections.Counter(item['status'] for item in bindings)
    statuses = ('host_bound', 'module_export_available', 'unbound')
    if set(counts) - set(statuses) or summary['imports'] != len(bindings) or any(summary[key] != counts[key] for key in statuses):
        raise ValueError('Census record counts do not match summary')
    images = [{key: item[key] for key in ('image_name', 'source_sha256', 'imports')}
              for item in events if item['event'] == 'image_prepared']
    if len({item['image_name'] for item in images}) != len(images) or len(images) != summary['images']:
        raise ValueError('Census image count mismatch')
    per_image = collections.Counter(item['image_name'] for item in bindings)
    if any(per_image[item['image_name']] != item['imports'] for item in images) or set(per_image) - {item['image_name'] for item in images}:
        raise ValueError('Census per-image imports mismatch')
    groups = collections.defaultdict(collections.Counter)
    for item in bindings:
        groups[item['library'], item['module']][item['status']] += 1
        item.pop('event', None)
    libraries = [dict(library=key[0], module=key[1], **{status: counts[status] for status in statuses})
                 for key, counts in sorted(groups.items())]
    return dict(format=1, summary=summary, images=images, libraries=libraries, bindings=bindings,
                interpretation='Bindings prove target availability, not API semantic completeness or path execution')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('census', type=Path)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--graph', type=Path)
    parser.add_argument('--image-name', default='eboot.bin', help='Exact census image name for ELF/graph analysis')
    parser.add_argument('--root', action='append', default=[])
    parser.add_argument('--follow-function-references', action='store_true',
                        help='Conservatively follow discovered function addresses, including possible callbacks')
    args = parser.parse_args()
    if bool(args.elf) != bool(args.graph) or (args.root and not args.graph):
        parser.error('--elf and --graph are required together; --root needs both')
    if args.image_name != 'eboot.bin' and not args.graph:
        parser.error('--image-name needs --elf and --graph')
    events = [json.loads(line) for line in args.census.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    report = summarize(events)
    if args.graph:
        graph = json.loads(args.graph.read_text(encoding='utf-8-sig'))
        elf = Elf(args.elf.read_bytes())
        inventory = elf.inventory()
        matching_images = [item for item in report['images'] if item['image_name'] == args.image_name]
        if len(matching_images) != 1:
            raise ValueError('Selected image must occur exactly once in the census')
        image = matching_images[0]
        if graph['sha256'].lower() != inventory['sha256'] or image['source_sha256'].lower() != inventory['sha256']:
            raise ValueError('ELF, graph and census must describe exactly the same original source hash')
        got_symbols = {item['address']: elf.symbols[item['symbol']]['name']
                       for item in elf.relocations if item['type'] in (6, 7)}

        def read_code(address, size):
            for segment in elf.segments:
                if segment['flags'] & 1 and segment['address'] <= address and address + size <= segment['address'] + segment['file_size']:
                    return elf.slice(segment['file_offset'] + address - segment['address'], size)
            return b''

        report['static_analysis'] = annotate(report['bindings'], graph, read_code, got_symbols, args.root,
                                             args.follow_function_references, args.image_name)
    print(json.dumps(report, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
