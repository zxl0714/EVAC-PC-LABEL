"""Build SDOJ's disc update from the same verified base and patches used at launch."""
import hashlib
import json
import re
import shutil
import tomllib

from project import reject_link


TITLE_UPDATE = 'tu101'


def checked_bytes(path, spec):
    reject_link(path)
    if not path.is_file() or path.stat().st_size != spec['bytes']:
        raise ValueError('Missing or incorrect title update file: ' + path.name)
    data = path.read_bytes()
    if len(data) != spec['bytes'] or hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError('Unrecognized title update file: ' + path.name)
    return data


def patch_inputs(data, update, container_override=None):
    container = container_override or data / update['container']['path']
    extracted = any((data / entry['path']).exists() for entry in update['patches'])
    verified = {}
    if container_override is not None or container.exists():
        package = checked_bytes(container, update['container'])
        for patch in update['patches']:
            offset = patch['offset']
            payload = package[offset:offset + patch['bytes']]
            if hashlib.sha256(payload).hexdigest() != patch['sha256']:
                raise ValueError('Unexpected title update payload: ' + patch['path'])
            verified[patch['path']] = payload
    if extracted:
        for patch in update['patches']:
            path = data / patch['path']
            padded = patch.get('padded')
            identity = padded if padded and path.is_file() and path.stat().st_size == padded['bytes'] else patch
            payload = checked_bytes(path, identity)[:patch['bytes']]
            if hashlib.sha256(payload).hexdigest() != patch['sha256']:
                raise ValueError('Unexpected title update payload: ' + patch['path'])
            verified[patch['path']] = payload
    if not verified:
        raise ValueError('SDOJ generation requires the disc TU 1.01 file or its three extracted patches')
    return verified


def stage_programs(records, data, folder, patches):
    folder.mkdir()
    for item in records.values():
        program = item['program_path']
        shutil.copyfile(data / program, folder / program)
    for name, payload in patches.items():
        (folder / name).write_bytes(payload)
    return folder


def title_update_configs(records, source, folder):
    """Rebind the reviewed base declarations to the corresponding TU 1.01 sites."""
    folder.mkdir()
    layouts = tomllib.loads((source / 'tu101-sites.toml').read_text(encoding='utf-8'))
    translated = {}
    for module, record in records.items():
        layout = layouts[module]
        entries = layout['addresses']
        sizes = layout.get('sizes', {})
        removed = set(layout.get('removed', []))
        used = set()
        lines = []
        visited = set()
        function_sites = set()

        def address(old):
            key = f'{old:08X}'
            used.add(key)
            return entries[key]

        def emit(name):
            if name in visited:
                return
            visited.add(name)
            raw = tomllib.loads((source / name).read_text(encoding='utf-8'))
            if set(raw) - {'includes', 'functions', 'midasm_hook'}:
                raise ValueError('Unreviewed SDOJ declaration: ' + name)
            for include in raw.get('includes', []):
                emit(include)
            for key, function in raw.get('functions', {}).items():
                old = int(key, 16)
                if f'{old:08X}' in removed:
                    continue
                new = address(old)
                if new in function_sites:
                    raise ValueError(f'Repeated TU 1.01 function at {new:08X}')
                function_sites.add(new)
                values = dict(function, size=sizes.get(f'{old:08X}', function['size']))
                if values.get('name', '').casefold() == f'sub_{old:08x}':
                    values['name'] = f'sub_{new:08X}'
                lines.append(f'[functions."0x{new:08X}"]')
                for field, value in values.items():
                    lines.append(f'{field} = {json.dumps(value)}')
                lines.append('')
            for hook in raw.get('midasm_hook', []):
                # Diagnostic hooks at the same instruction are already merged
                # by our production full-context adapter where applicable.
                lines.append('[[midasm_hook]]')
                for field, value in hook.items():
                    if field == 'address' or field.startswith('jump_address'):
                        lines.append(f'{field} = 0x{address(value):08X}')
                    else:
                        lines.append(f'{field} = {json.dumps(value)}')
                lines.append('')

        for include in record['includes']:
            emit(include)
        if used != set(entries):
            raise ValueError(f'Unused TU 1.01 address entries in {module}: {sorted(set(entries) - used)}')
        for new, size in layout.get('extra_functions', {}).items():
            if not re.fullmatch(r'[0-9A-F]{8}', new) or type(size) is not int or size <= 0 or size % 4 or int(new, 16) in function_sites:
                raise ValueError('Invalid TU 1.01 extra function')
            lines.extend([f'[functions."0x{new}"]', f'size = {size}', ''])
        name = module + '.toml'
        (folder / name).write_text('\n'.join(lines), encoding='utf-8', newline='\n')
        translated[module] = dict(record, includes=[name])
    return translated
