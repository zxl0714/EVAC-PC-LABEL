"""Prepare an offline native-loader probe input; all output is local game data."""

import argparse
import hashlib
import json
import struct
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from analysis_paths import private_output

from native_elf import Elf, nid, read_names


def verified_init_env(libc):
    raw = libc.read_bytes()
    elf = Elf(raw)
    matches = [s for s in elf.symbols if s["section"] and s["type"] == 2
               and s["name"].split("#")[0] == nid("_init_env")]
    if len(matches) != 1 or matches[0]["size"] != 1:
        raise ValueError("libc _init_env is not a unique one-byte function; inspect its contract first")
    symbol = matches[0]
    segment = next((s for s in elf.segments if s["flags"] & 1
                    and s["address"] <= symbol["value"] < s["address"] + s["file_size"]), None)
    if not segment:
        raise ValueError("libc _init_env is outside executable file bytes")
    offset = segment["file_offset"] + symbol["value"] - segment["address"]
    if elf.slice(offset, 1) != b"\xc3":
        raise ValueError("libc _init_env has behavior other than RET; a no-op binding is invalid")
    digest = hashlib.sha256(raw).hexdigest()
    if hashlib.sha256(libc.read_bytes()).hexdigest() != digest:
        raise ValueError("libc changed during contract verification")
    return dict(capability="libc_init_env_ret", source=str(libc), sha256=digest,
                nid=nid("_init_env"), address=symbol["value"], size=1, instruction="ret")


def prepare(source, names, libc=None, module=False):
    raw = source.read_bytes()
    elf = Elf(raw)
    inventory = elf.inventory(names)
    if not module and (elf.elf_type != 0xFE10 or not elf.entry):
        raise ValueError("The startup probe currently accepts an eboot executable only")
    if module and (elf.elf_type != 0xFE18 or elf.entry or elf.procparam):
        raise ValueError('The module loader currently accepts PRX with no ELF entry or process parameters')
    if any(t["memory_size"] for t in inventory["tls"]):
        raise ValueError("The startup probe does not yet initialize static TLS")
    exports = elf.exports(names) if module else []
    module_info = None
    if module:
        if elf.tags.get(27, 0) or elf.tags.get(28, 0):
            raise ValueError('Standalone module init/fini arrays require an explicit startup contract')
        flags = (1 if 12 in elf.tags else 0) | (2 if 13 in elf.tags else 0)
        if not flags & 1:
            raise ValueError('PRX has no DT_INIT startup contract')
        for tag in (12, 13):
            if tag in elf.tags and not elf.contains(elf.tags[tag], 1, executable=True):
                raise ValueError('Module initialization or finalization address is outside executable file bytes')
        module_info = dict(init=elf.tags.get(12, 0), fini=elf.tags.get(13, 0), flags=flags,
                           exports=len(exports))
    referenced_symbols = {relocation["symbol"] for relocation in elf.relocations}
    # An undefined weak STT_NOTYPE symbol with no relocation is only a symbol
    # table declaration, not a runtime dependency. Preserve it in the audit;
    # do not invent a function/data kind or weaken referenced-symbol checks.
    omitted = [symbol for symbol in inventory["imports"]
               if symbol["type"] == 0 and symbol["binding"] == 2 and symbol["index"] not in referenced_symbols]
    omitted_indices = {symbol["index"] for symbol in omitted}
    imports = [symbol for symbol in inventory["imports"] if symbol["index"] not in omitted_indices]
    if any(symbol["type"] not in (1, 2) for symbol in imports):
        raise ValueError("Unsupported import symbol kind; referenced untyped weak symbols require an explicit resolution contract")
    inventory["imports"] = imports
    inventory["omitted_unreferenced_weak_imports"] = omitted
    contracts = [verified_init_env(libc)] if libc else []
    import_indices = {symbol["index"]: i for i, symbol in enumerate(imports)}
    relocations = []
    for relocation in elf.relocations:
        kind, value, addend = 0, 0, relocation["addend"]
        if relocation["type"] == 8:
            if relocation["symbol"]:
                raise ValueError("RELATIVE relocation unexpectedly names a symbol")
        elif relocation["type"] in (1, 6, 7):
            symbol = elf.symbols[relocation["symbol"]]
            if symbol["section"] == 0:
                if relocation["symbol"] not in import_indices:
                    raise ValueError("Relocation references an empty undefined symbol")
                kind, value = 1, import_indices[relocation["symbol"]]
            elif symbol["section"] == 0xFFF1:
                kind, value = 2, symbol["value"]
            else:
                if not elf.contains(symbol["value"], max(1, symbol["size"])):
                    raise ValueError("Defined relocation symbol is outside mapped segments")
                value = symbol["value"]
        else:
            raise ValueError(f"Unsupported relocation type {relocation['type']}")
        relocations.append((relocation["address"], kind, value, addend))
    output = bytearray(struct.pack("<8s7Q32s", b"KTMODULE" if module else b"KTPROBE1", elf.image_size, elf.entry,
                                  len(elf.segments), len(relocations), len(imports), elf.procparam,
                                  1 if contracts else 0, hashlib.sha256(raw).digest()))
    if module:
        output += struct.pack('<4Q', module_info['init'], module_info['fini'], module_info['flags'], len(exports))
    for segment in elf.segments:
        output += struct.pack("<4Q", segment["address"], segment["memory_size"],
                              segment["flags"], segment["file_size"])
        output += elf.slice(segment["file_offset"], segment["file_size"])
    for symbol in imports:
        for field in ("name", "friendly", "library", "module"):
            data = symbol[field].encode("utf-8")
            output += struct.pack("<I", len(data)) + data
        output += struct.pack("<I", symbol["type"])
    for symbol in exports:
        for field in ('name', 'friendly', 'library', 'module'):
            data = symbol[field].encode('utf-8')
            output += struct.pack('<I', len(data)) + data
        output += struct.pack('<IIQQ', symbol['type'], symbol['binding'], symbol['value'], symbol['size'])
    for relocation in relocations:
        output += struct.pack("<3Qq", *relocation)
    if hashlib.sha256(source.read_bytes()).hexdigest() != inventory["sha256"]:
        raise ValueError("Source changed during preparation")
    inventory["probe_format"] = "KTMODULE" if module else "KTPROBE1"
    inventory['source'] = str(source.resolve())
    if module:
        inventory['module'] = module_info
        inventory['exports'] = exports
    inventory["probe_sha256"] = hashlib.sha256(output).hexdigest()
    inventory["contracts"] = contracts
    inventory["preparer_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    inventory["elf_reader_sha256"] = hashlib.sha256(Path(__file__).with_name("native_elf.py").read_bytes()).hexdigest()
    return output, inventory


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--names", type=Path)
    parser.add_argument("--libc", type=Path, help="Verify the local libc _init_env RET contract")
    parser.add_argument('--module', action='store_true', help='Prepare a no-TLS PRX with explicit DT_INIT and exports')
    args = parser.parse_args()
    source = args.input.resolve()
    destination = private_output(args.output, source)
    private_output(destination.with_suffix('.json'), source)
    if destination == source or destination.exists() or destination.with_suffix(".json").exists():
        raise ValueError("Output must be a new file, distinct from the source")
    data, inventory = prepare(source, read_names(args.names) if args.names else {},
                              args.libc.resolve() if args.libc else None, module=args.module)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(data)
    with destination.with_suffix(".json").open("x", encoding="utf-8") as stream:
        json.dump(inventory, stream, ensure_ascii=False, indent=2)
    print(json.dumps(dict(input=str(source), output=str(destination), imports=len(inventory["imports"]),
                          relocations=inventory["relocation_count"], sha256=inventory["sha256"])))


if __name__ == "__main__":
    main()
