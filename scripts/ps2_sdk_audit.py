"""Audit a game's identified SDK inventory against its generated native routes."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import tomllib

from project import ROOT, digest, write_json_new
from ps2_codegen import validate_sdk_bindings, validate_sdk_lifecycles
from ps2_module_extras import guest_branch_targets, table_entries


def selectors(general):
    result=defaultdict(set)
    for key in ('stubs','entry_points','untracked_stubs'):
        for value in general.get(key,[]):
            name,separator,address=value.rpartition('@')
            if separator:
                result[int(address,0)].add(name)
    return result


def review_native_handlers(entries):
    # Local runtime overrides take precedence over the pinned upstream files.
    upstream=ROOT/'.deps/ps2/PS2Recomp/ps2xRuntime/src/lib/Kernel'
    overrides=ROOT/'third_party/ps2/runtime'
    replaced={path.name for path in overrides.glob('*.cpp')}
    files=[path for path in upstream.rglob('*.cpp') if path.name not in replaced]
    files.extend(overrides.glob('*.cpp'))
    bodies={}
    pattern=re.compile(r'\bvoid\s+(\w+)\s*\([^)]*\)\s*\{')
    for path in files:
        text=path.read_text(encoding='utf-8')
        functions=list(pattern.finditer(text))
        for index,function in enumerate(functions):
            end=functions[index+1].start() if index+1<len(functions) else len(text)
            bodies[function[1]]=(path,text[function.end():end])
    reviews=[]
    used={handler for entry in entries for handler in entry['nativeHandlers']}
    for name in sorted(used & bodies.keys()):
        path,body=bodies[name]
        stripped=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
        reason=None
        if 'TODO_NAMED(' in stripped:
            reason='contains_explicit_todo'
        else:
            minimal=re.sub(r'\(void\)\s*\w+\s*;','',stripped)
            minimal=re.sub(r'setReturn\w*\(ctx,\s*(?:[-+]?\d+|0x[0-9a-fA-F]+|true|false)[uUlL]*\s*\);','',minimal)
            if not re.sub(r'[\s{};]','',minimal):
                reason='return_only_review_semantics'
        if reason:
            related=[entry for entry in entries if name in entry['nativeHandlers']]
            reviews.append(dict(handler=name,reason=reason,source=path.relative_to(ROOT).as_posix(),
                                callers=sorted({caller for entry in related for caller in entry['callers']})))
    return reviews


def audit(config, analysis):
    config,analysis=map(Path,(config,analysis))
    general=tomllib.loads(config.read_text(encoding='utf-8'))['general']
    identified=tomllib.loads(analysis.read_text(encoding='utf-8'))['general']
    binding_path=Path(general['sdk_bindings'])
    bindings=binding_path.read_text(encoding='utf-8')
    validate_sdk_bindings(general,bindings)
    lifecycles=validate_sdk_lifecycles(general,bindings)
    inventory=selectors(identified)
    for address,names in selectors(general).items():
        inventory[address].update(names)
    bound={int(address,16):name for address,name in re.findall(
        r'BindSdkHandler\(runtime,0x([0-9a-fA-F]+)u,"([^"]+)"\)',bindings)}
    for address,name in bound.items():
        inventory[address].add(name)
    custom={int(address,16):name for address,name in re.findall(
        r'replaceFunction\(0x([0-9a-fA-F]+)u,&cave::ps2::SdkEntry<&(\w+)>\)',bindings)}
    base=Path(general['output'])
    table_text=(base/'register_functions.cpp').read_text(encoding='utf-8')
    limits=[re.search(r'g_ps2RecompiledFunctionTable'+name+r' = 0x([0-9a-fA-F]+)u',table_text)
            for name in ('Base','End')]
    if any(limit is None for limit in limits):
        raise ValueError('No dense-table range for runtime SDK bindings')
    begin,end=[int(limit[1],16) for limit in limits]
    linked=table_entries(base/'register_functions.cpp')
    if not linked:
        raise ValueError('No generated function table')
    routes={};callers=defaultdict(set)
    # Read each native body once, even when the dispatch table has many resume slots.
    for symbol in sorted(set(linked.values())):
        source=base/(symbol+'.cpp')
        if not source.is_file():
            raise ValueError('Missing generated SDK-audit input: '+str(source))
        text=source.read_text(encoding='utf-8')
        targets=guest_branch_targets(text)
        targets.update(int(pc,16) for pc in re.findall(r'\b\w+_0x([0-9a-fA-F]+)\(',text))
        for target in targets & inventory.keys():
            if linked.get(target)!=symbol:
                callers[target].add(symbol)
        native=sorted(set(re.findall(r'\b(?:ps2_stubs|ps2_syscalls)::(\w+)\(',text)))
        routes[symbol]=dict(native=native,targets=targets)
    rpc={address for address,names in inventory.items()
         if any(name in ('sceSifCallRpc','SifCallRpc') for name in names)}
    entries=[];counts=defaultdict(int)
    selected={int(value.rsplit('@',1)[1],0) for value in general.get('stubs',[]) if '@' in value}
    mismatches=[]
    for address,names in sorted(inventory.items()):
        symbol=linked.get(address)
        route=routes.get(symbol,{})
        native=route.get('native',[])
        runtime_bound=address in (bound.keys()|custom.keys()) and begin<=address<end and address%4==0
        if runtime_bound and not native:
            status='native_runtime'
            native=[bound.get(address,custom.get(address))]
        elif not symbol:
            status='not_compiled'
        elif native:
            status='native'
        else:
            status='original_guest'
        if address in selected and status not in ('native','native_runtime'):
            mismatches.append(dict(address=hex(address),names=sorted(names),status=status))
        counts[status]+=1
        entries.append(dict(address=f'0x{address:08x}',names=sorted(names),status=status,
                            nativeHandlers=native,callers=sorted(callers[address]),
                            runtimeBound=runtime_bound,
                            directRpc=bool(route.get('targets',set()) & rpc)))
    reviews=[entry for entry in entries if entry['directRpc'] and entry['status']=='original_guest']
    return dict(format=1,configSha256=digest(config),analysisSha256=digest(analysis),
                sdkBindingsSha256=digest(binding_path),identifiedEntries=len(inventory),
                counts=dict(counts),lifecycles=lifecycles,selectedRouteMismatches=mismatches,
                retainedRpcClients=reviews,nativeSemanticReviews=review_native_handlers(entries),entries=entries,
                scope='Identified SDK entries in the supplied analysis/config; computed callbacks and unknown SDK functions require separate review')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config',type=Path)
    parser.add_argument('--analysis',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    result=audit(args.config,args.analysis)
    write_json_new(args.output,result)
    print(json.dumps({key:value for key,value in result.items()
                      if key not in ('entries','retainedRpcClients','nativeSemanticReviews','lifecycles')},indent=2))
    print(f'Retained original RPC clients for review: {len(result["retainedRpcClients"])}')
    raise SystemExit(bool(result['selectedRouteMismatches']))
