"""Wire the separately translated R-Type Final code banks into the native host."""

import argparse
import json
from pathlib import Path

from project import ROOT, digest, reject_link
from ps2_translation_units import write_changed


def configure(manifest, output):
    value = json.loads(manifest.read_text(encoding='utf-8'))
    if value.get('format') != 1 or not 1 <= len(value.get('banks', ())) <= 32:
        raise ValueError('Invalid R-Type Final code bank manifest')
    cmake = []
    declarations = []
    registrations = []
    seen = set()
    for bank in value['banks']:
        identity = tuple(bank[key] for key in ('id', 'address', 'textSize', 'dataSize', 'bssSize'))
        number, address, text_size, data_size, bss_size = identity
        if (any(type(field) is not int or field < 0 for field in identity)
                or number in seen or not 1 <= number <= 39 or not text_size
                or (address | text_size | data_size | bss_size) & 3
                or address < 0x100000 or address + 64 + text_size + data_size + bss_size > 0x2000000):
            raise ValueError('Invalid R-Type Final MWo3 bank')
        seen.add(number)
        directory = Path(bank['generated']).resolve()
        directory.relative_to(ROOT.resolve())
        receipt = directory.parent / 'codegen.json'
        for path in (directory, receipt, directory / 'register_functions.cpp'):
            reject_link(path)
        summary = json.loads(receipt.read_text(encoding='utf-8'))['summary']
        if (digest(receipt) != bank['receiptSha256'] or any(summary[key] for key in
                ('errors', 'decodeFailures', 'unhandledInstructions', 'criticalGuestFallbacks'))):
            raise ValueError(f'Unverified R-Type Final bank {number}')
        prefix = f'ps2_rtype_final_{number:02}'
        table = f'{prefix}_table'
        cmake.append(f'ps2_add_disc_program(ps2_rtype_final_probe {prefix} "{directory.as_posix()}")')
        declarations.extend((
            f'extern const uint32_t {table}Base;',
            f'extern const uint32_t {table}SlotCount;',
            f'extern PS2Runtime::RecompiledFunction {table}[];',
        ))
        fields = ', '.join(hex(field) for field in identity)
        registrations.append(
            f'    runtime.registerStaticModule({{{fields},\n'
            f'        {table}Base, {{{table}, {table}SlotCount}}}});')

    output.mkdir(parents=True, exist_ok=True)
    glue = output / 'registration.cpp'
    write_changed(glue, '\n'.join((
        '#include "ps2_runtime.h"', '', *declarations, '',
        'void RegisterRTypeFinalStaticModules(PS2Runtime& runtime) {', *registrations, '}', '',
    )))
    cmake.append(f'target_sources(ps2_rtype_final_probe PRIVATE "{glue.as_posix()}")')
    write_changed(output / 'modules.cmake', '\n'.join(cmake) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    configure(args.manifest, args.output)
