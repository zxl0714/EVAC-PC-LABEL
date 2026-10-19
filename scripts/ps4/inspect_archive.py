"""Validate a user-supplied PS4 data ZIP; optionally copy into a new local analysis directory.

Does not execute programs, modify the archive, select a supported game version,
or mark a title playable. Every file is CRC-checked and hashed before success.
"""
import argparse
import hashlib
from pathlib import Path
import re
import stat
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import analysis_child, private_output
from generate_catalogue import relative
from project import digest, reject_link, write_json_new
from ps4_inventory import parse_sfo

MAX_FILES = 4096
MAX_FILE_BYTES = 4 * 1024**3
MAX_TOTAL_BYTES = 8 * 1024**3
RESERVED = re.compile(r'(?i)^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)')


def members(archive):
    entries = archive.infolist()
    if not 1 <= len(entries) <= MAX_FILES:
        raise ValueError('Invalid archive member count')
    names, files, directories = set(), {}, set()
    total = 0
    for entry in entries:
        name = relative(entry.filename.rstrip('/') if entry.is_dir() else entry.filename)
        parts = name.split('/')
        if any(RESERVED.match(p) or p.casefold() == '.git' for p in parts):
            raise ValueError('Reserved archive path')
        folded = name.casefold()
        if folded in names:
            raise ValueError('Duplicate or case-colliding archive member')
        names.add(folded)
        mode = stat.S_IFMT(entry.external_attr >> 16)
        if mode not in (0, stat.S_IFDIR if entry.is_dir() else stat.S_IFREG):
            raise ValueError('Archive links and special files are not supported')
        if entry.flag_bits & 1:
            raise ValueError('Encrypted ZIP entries are not supported')
        if entry.is_dir():
            directories.add(folded)
        else:
            if entry.file_size < 0 or entry.file_size > MAX_FILE_BYTES:
                raise ValueError('Archive file exceeds inspection limit')
            total += entry.file_size
            if total > MAX_TOTAL_BYTES:
                raise ValueError('Archive exceeds total inspection limit')
            files[folded] = (name, entry)
    for name in names:
        for parent in Path(name).parents:
            if parent.as_posix() in files:
                raise ValueError('Archive file shadows a directory')
    metadata = [value for key, value in files.items() if key.endswith('/sce_sys/param.sfo') or key == 'sce_sys/param.sfo']
    if len(metadata) != 1:
        raise ValueError('Expected exactly one title SFO')
    metadata_name, _ = metadata[0]
    prefix = metadata_name[:-len('sce_sys/param.sfo')]
    if prefix and any(not name.startswith(prefix) for name, _ in files.values()):
        raise ValueError('Archive contains files outside the title directory')
    if (prefix + 'eboot.bin').casefold() not in files:
        raise ValueError('Missing title executable')
    result = [(name[len(prefix):], entry) for name, entry in files.values()]
    if any(Path(name).suffix.casefold() in ('.exe', '.dll', '.bat', '.cmd', '.ps1', '.lnk') for name, _ in result):
        raise ValueError('Archive includes non-game host programs or scripts')
    return sorted(result), prefix


def inspect(source, destination=None):
    source = Path(source).absolute()
    reject_link(source)
    before = source.stat()
    if destination is not None:
        destination = private_output(destination, source)
    with zipfile.ZipFile(source) as archive:
        entries, prefix = members(archive)
        metadata_entry = next(entry for name, entry in entries if name == 'sce_sys/param.sfo')
        if metadata_entry.file_size > 1024**2:
            raise ValueError('Invalid SFO size')
        metadata = parse_sfo(archive.read(metadata_entry))
        if not re.fullmatch(r'CUSA[0-9]{5}', metadata.get('TITLE_ID', '')):
            raise ValueError('Expected a PS4 title identity')
        if destination is not None:
            destination.mkdir(parents=True, exist_ok=False)
        records = []
        for name, entry in entries:
            output = None
            if destination is not None:
                target = analysis_child(destination, name)
                target.parent.mkdir(parents=True, exist_ok=True)
                output = target.open('xb')
            sha, size = hashlib.sha256(), 0
            try:
                with archive.open(entry) as stream:
                    while chunk := stream.read(1024**2):
                        size += len(chunk)
                        if size > entry.file_size:
                            raise ValueError('Archive member exceeded declared size')
                        sha.update(chunk)
                        if output is not None:
                            output.write(chunk)
            finally:
                if output is not None:
                    output.close()
            if size != entry.file_size:
                raise ValueError('Truncated archive member')
            records.append(dict(path=name, bytes=size, sha256=sha.hexdigest(),
                                program=name == 'eboot.bin' or name.lower().endswith('.prx')))
    archive_sha = digest(source)
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('Archive changed during inspection; retry after download completes')
    return dict(format=1, platform='ps4', title=metadata, files=records,
                source=dict(path=str(source), bytes=after.st_size, sha256=archive_sha, prefix=prefix),
                extractedDirectory=str(destination) if destination else None,
                crcVerified=True, guestExecution=False, supportedVersionVerified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--extract-to', type=Path)
    args = parser.parse_args()
    output = private_output(args.output, args.archive)
    if args.extract_to is not None:
        target = private_output(args.extract_to, args.archive)
        if output.is_relative_to(target) or target.is_relative_to(output):
            raise ValueError('Keep the report separate from extracted title data')
    result = inspect(args.archive, args.extract_to)
    write_json_new(output, result)
    print(f'{result["title"]["TITLE_ID"]}: {len(result["files"])} CRC-verified files; no guest execution')


if __name__ == '__main__':
    main()
