"""Relink G2 probes using existing objects; all new outputs stay private."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
import shutil

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from toolchain import tool_path
from rebuild_ps4_probe import normalized, split_windows


def compile_command(entry, source, output, includes, host=False):
    command, changed = [], set()
    args = iter(split_windows(entry['command']))
    for arg in args:
        if arg in ('-o', '-MT', '-MF'):
            next(args)
            command.extend([arg, str(output) + ('.d' if arg == '-MF' else '')])
            changed.add(arg)
        elif normalized(arg) == normalized(entry['file']):
            command.append(str(source)); changed.add('source')
        else:
            command.append(arg)
    if not {'-o', 'source'} <= changed:
        raise ValueError('Compile output/source redirection failed')
    command[1:1] = ['-I' + str(p) for p in includes] + (['-DEVAC_LATENCY_HOST=1'] if host else [])
    return command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--reuse', type=Path)
    a = p.parse_args()
    fixture, destination = a.fixture.resolve(), a.destination.resolve()
    private = ROOT / '.local/input-latency-v080-r1'
    if not all(path.is_relative_to(private) for path in (fixture, destination)):
        raise ValueError('Private paths required')
    receipt = json.loads((fixture / 'preparation.json').read_text())
    for path, expected in receipt['source_inputs'].items():
        if digest(Path(path)) != expected:
            raise ValueError('Original source changed after preparation')
    destination.mkdir(exist_ok=False)
    build = ROOT / 'out/xbox360'
    ninja = str(tool_path('ninja'))
    def database(rule):
        return json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb', '-x', rule], encoding='utf-8'))
    compile_steps, links, hashes = [], [], {}
    def compile_entry(entry, source, label, includes, host=False):
        output = destination / (label + '.obj')
        command = compile_command(entry, source, output, includes, host)
        hashes[str(Path(entry['file']).resolve())] = digest(Path(entry['file']))
        hashes[str(source)] = digest(source)
        compile_steps.append(dict(stage='compile', source=str(source), output=str(output), command=command))
        return str(output)
    input_entries = database('CXX_COMPILER__cave_xbox360_input_unscanned_Release')
    if {Path(e['file']).name for e in input_entries} != {'configured_input.cpp', 'input_state.cpp'}:
        raise ValueError('Shared input library composition changed')
    input_entry = next(e for e in input_entries if Path(e['file']).name == 'configured_input.cpp')
    includes = [ROOT / 'tools/latency', ROOT / 'backends/xbox360/input']
    input_obj = compile_entry(input_entry, fixture / 'configured_input.cpp', 'configured_input', includes, True)
    unchanged_input = next(e['output'] for e in input_entries if Path(e['file']).name == 'input_state.cpp')
    for module, target in (('default', 'espgaluda2'), ('dat12', 'espgaluda2_dat12')):
        entries = database('CXX_COMPILER__' + target + '_unscanned_Release')
        replacements = {}
        extension = '.exe' if module == 'default' else '.dll'
        inc = [fixture / 'generated' / module, ROOT / 'tools/latency', ROOT / 'games/espgaluda2']
        for name in receipt['changed_cpp'][module]:
            entry, = [e for e in entries if Path(e['file']).name == name and '/generated/' in normalized(e['file'])]
            replacements[normalized(entry['output'])] = [compile_entry(entry, fixture / 'generated' / module / name,
                module + '-' + name, inc, module == 'default')]
        template, = [e for e in entries if Path(e['file']).name == 'espgaluda2_recomp.0.cpp']
        extras = [compile_entry(template, fixture / (module + '-probe.cpp'), module + '-bridge', inc, module == 'default')]
        if module == 'default':
            host_entry, = [e for e in entries if normalized(e['file']) == normalized(ROOT / 'games/espgaluda2/host.cpp')]
            replacements[normalized(host_entry['output'])] = [compile_entry(host_entry, fixture / 'host.cpp', 'host', inc, True)]
            extras.append(compile_entry(host_entry, ROOT / 'tools/latency/futari_probe_host.cpp', 'probe-host', inc, True))
            replacements[normalized('backends/xbox360/cave_xbox360_input.lib')] = [input_obj, unchanged_input]
        kind = 'CXX_EXECUTABLE_LINKER__' if module == 'default' else 'CXX_SHARED_LIBRARY_LINKER__'
        entry, = [e for e in database(kind + target + '_Release') if Path(e['output']).name == target + extension]
        pieces = entry['command'].split(' && ')
        # Ignore production post-link DLL copies. Only run the link command,
        # whose outputs are redirected below; never execute the outer wrapper.
        if len(pieces) < 3 or not pieces[0].endswith('/C "cd .'):
            raise ValueError('Unexpected CMake link wrapper')
        if normalized(split_windows(pieces[1])[0]) != normalized(tool_path('clang++')):
            raise ValueError('Link recipe does not use the pinned compiler')
        command, modified, outputs = [], set(), set()
        args = iter(split_windows(pieces[1]))
        for arg in args:
            n = normalized(arg)
            if arg == '-o':
                next(args); command.extend(['-o', str(destination / (target + extension))]); outputs.add('binary')
            elif n.startswith('/implib:'):
                command.append('/implib:' + str(destination / (target + '.lib'))); outputs.add('lib')
            elif n.startswith('/pdb:'):
                command.append('/pdb:' + str(destination / (target + '.pdb'))); outputs.add('pdb')
            elif n in replacements:
                command.extend(replacements[n]); modified.add(n)
            else:
                command.append(arg)
            path = Path(arg) if Path(arg).is_absolute() else build / arg
            if n.endswith(('.obj', '.lib')) and path.is_file():
                hashes[str(path.resolve())] = digest(path)
        if outputs != {'binary', 'lib', 'pdb'} or modified != replacements.keys():
            raise ValueError('Incomplete link redirection/replacements')
        command.extend(extras)
        links.append(dict(stage='link', output=str(destination / (target + extension)), command=command))
    # Include the shared observer headers used by the generated bridges.
    for path in (ROOT / 'tools/latency').glob('*.h'):
        hashes[str(path)] = digest(path)
    for path in (fixture / 'generated').rglob('*.h'):
        hashes[str(path)] = digest(path)
    if a.reuse:
        old = a.reuse.resolve()
        if not old.is_relative_to(private):
            raise ValueError('Reuse build must be private')
        old_plan = json.loads((old / 'build-plan.json').read_text())
        old_result = json.loads((old / 'build-result.json').read_text())
        old_fixture = Path(next(s['source'] for s in old_plan['compile'] if Path(s['source']).name == 'host.cpp')).parent
        # Header declarations used by reused objects must also be byte-identical.
        for path in (fixture / 'generated').rglob('*.h'):
            prior = old_fixture / path.relative_to(fixture)
            if not prior.is_file() or digest(prior) != digest(path):
                raise ValueError('Cannot reuse objects with changed generated headers')
        old_by_output = {Path(s['output']).name:s for s in old_plan['compile']}
        for step in compile_steps:
            name = Path(step['output']).name
            previous = old_by_output.get(name)
            if not previous or digest(Path(step['source'])) != old_plan['input_sha256'].get(previous['source']):
                continue
            def canonical(command, fixture_root, build_root):
                return [normalized(arg).replace(normalized(fixture_root), '<fixture>')
                        .replace(normalized(build_root), '<build>') for arg in command]
            if canonical(step['command'], fixture, destination) != canonical(previous['command'], old_fixture, old):
                continue
            prior = Path(previous['output'])
            if digest(prior) != old_result['files'].get(name):
                raise ValueError('Prior private object identity changed')
            shutil.copy2(prior, step['output'])
            step['reused_from'] = str(prior)
    write_json_new(destination / 'build-plan.json', dict(cwd=str(build), compile=compile_steps, links=links,
        input_sha256=hashes, preparation_sha256=digest(fixture / 'preparation.json')))
    def run(step):
        if step.get('reused_from'):
            print('reuse: ' + Path(step['output']).name, flush=True)
            return
        print(step['stage'] + ': ' + Path(step['output']).name, flush=True)
        subprocess.run(step['command'], cwd=build, check=True)
    # Two bounded compilers. Parent is launched BelowNormal by build wrapper.
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, compile_steps))
    for step in links:
        run(step)
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise ValueError('Original input changed during build: ' + path)
    write_json_new(destination / 'build-result.json', dict(
        files={Path(s['output']).name: digest(Path(s['output'])) for s in compile_steps + links},
        original_inputs_unchanged=True, release_certified=False))
    print('Private G2 measurement build complete', flush=True)


if __name__ == '__main__':
    main()
