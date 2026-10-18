# Adapted from ESPGaluda2-PC ba85ac0, scripts/project.py.
"""Read bounded XEX headers and data inventories; never execute or copy a game."""
import argparse
import json
import os
from pathlib import Path
import struct

from project import digest, reject_link, write_json_new
from analysis_paths import private_output


def files(root):
    root = Path(root).absolute()
    reject_link(root)
    if not root.is_dir():
        raise ValueError(f'Not a directory: {root}')
    for current, directories, names in os.walk(root, followlinks=False):
        directories.sort()
        for name in directories + sorted(names):
            path = Path(current) / name
            reject_link(path)
            if name in names:
                if not path.is_file():
                    raise ValueError(f'Non-regular input: {path}')
                yield path


def xex_metadata(path):
    # XEX2 layout: maintained ReXGlue xex2_info.h. No decryption or translation.
    size = Path(path).stat().st_size
    with Path(path).open('rb') as source:
        data = source.read(min(size, 1024 * 1024))
    if len(data) < 24 or data[:4] != b'XEX2':
        raise ValueError(f'Not an XEX2 header: {Path(path).name}')
    _, flags, header_size, _, _, count = struct.unpack_from('>6I', data)
    if count > 4096 or 24 + count * 8 > len(data) or not 24 + count * 8 <= header_size <= size:
        raise ValueError('Invalid or unsupported XEX header bounds')
    options = {}
    for index in range(count):
        key, value = struct.unpack_from('>2I', data, 24 + index * 8)
        if key in options:
            raise ValueError('Duplicate XEX optional header')
        options[key] = value

    def read_at(offset, length):
        if offset < 24 + count * 8 or offset + length > min(len(data), header_size):
            raise ValueError('XEX optional header is outside the bounded header')
        return data[offset:offset + length]

    result = dict(moduleFlags=f'0x{flags:08X}', headerBytes=header_size,
                  imageBase=None, entryPoint=None, titleId=None, versionRaw=None,
                  encryption=None, compression=None)
    for key, name in [(0x00010201, 'imageBase'), (0x00010100, 'entryPoint')]:
        if key in options:
            result[name] = f'0x{options[key]:08X}'
    if 0x00040006 in options:
        execution = read_at(options[0x00040006], 24)
        _, version, base_version, title = struct.unpack_from('>4I', execution)
        result.update(titleId=f'{title:08X}', versionRaw=f'0x{version:08X}', baseVersionRaw=f'0x{base_version:08X}')
    if 0x000003FF in options:
        info_size, encryption, compression = struct.unpack('>IHH', read_at(options[0x000003FF], 8))
        if info_size < 8:
            raise ValueError('Invalid XEX file-format header')
        read_at(options[0x000003FF], info_size)
        result.update(encryption=encryption, compression=compression)
    return result


def inventory(root):
    root = Path(root).absolute()
    records, modules = [], []
    for path in sorted(files(root)):
        size = path.stat().st_size
        item = dict(path=path.relative_to(root).as_posix(), bytes=size, sha256=digest(path))
        if path.stat().st_size != size:
            raise ValueError('Input changed during inventory')
        records.append(item)
        if path.suffix.lower() == '.xex':
            modules.append(dict(item, **xex_metadata(path)))
    if not any(item['path'].lower() == 'default.xex' for item in modules):
        raise ValueError('Expected default.xex at the game-data root')
    return dict(format=1, fileCount=len(records), totalBytes=sum(item['bytes'] for item in records), files=records, xex=modules)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--xex-lock', type=Path)
    args = parser.parse_args()
    outputs = [private_output(path, args.directory) if path else None for path in (args.output, args.xex_lock)]
    if outputs[0] and outputs[0] == outputs[1]:
        raise ValueError('Use separate report and program-lock paths')
    result = inventory(args.directory.resolve())
    if outputs[0]:
        write_json_new(outputs[0], result)
    if outputs[1]:
        write_json_new(outputs[1], dict(format=1, algorithm='sha256', xex=result['xex']))
    print(json.dumps(dict(fileCount=result['fileCount'], totalBytes=result['totalBytes'], xex=result['xex'], guestExecution=False), indent=2))


if __name__ == '__main__':
    main()
