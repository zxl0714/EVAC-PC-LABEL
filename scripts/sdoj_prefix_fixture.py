"""Prepare a local, non-playable native input-prefix fixture from generated code.

No game body is stored in this script or in Git. The output stays under .local
or out; it contains only the two prefix cuts and their native save/restore ABI.
Engine action/config getters are supplied by the synthetic C++ test, not devices.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def function(source: str, name: str) -> str:
    match = re.search(r'^DEFINE_REX_FUNC\('+re.escape(name)+r'\) \{.*?(?=^DEFINE_REX_FUNC|\Z)',
                      source, re.M | re.S)
    if not match:
        raise ValueError(f'missing generated function: {name}')
    body = match[0].strip()
    # Trailing declarations between generated functions do not belong to it.
    return body[:body.rfind('\n}')+2]


def prepare(module: str, output: Path) -> dict:
    output = output.resolve()
    if not any(output.is_relative_to(ROOT / folder) for folder in ('.local', 'out')):
        raise ValueError('derived game code must remain in .local or out')
    generated = ROOT / 'generated/sdoj' / module
    first = (generated/'sdoj_recomp.0.cpp').read_text(encoding='utf-8')
    floats = next((text for path in sorted(generated.glob('sdoj_recomp.*.cpp'))
                   if 'DEFINE_REX_FUNC(__savefpr_28)' in
                   (text := path.read_text(encoding='utf-8'))), None)
    if floats is None:
        raise ValueError('missing generated FPR helpers')
    native = function(first, 'sub_88044960')
    epilogue = '88045DC0' if module=='CA022100' else '88045D70'
    cut = native.index('loc_880451D4:')
    cleanup = native.index('loc_'+epilogue+':')
    prefix = native[:cut] + ('loc_880451D4:\n'
             '\tSdojDelayInputDone(ctx, base);\n\tgoto loc_'+epilogue+';\n') + native[cleanup:]
    if prefix.count('SdojDelayInputStart(ctx, base)')!=1 or prefix.count('SdojDelayInputDone(ctx, base)')!=1:
        raise ValueError('native prefix hook layout changed')
    pieces = [function(first, name) for name in ('__savegprlr_20','__restgprlr_20')]
    pieces += [function(floats, name) for name in ('__savefpr_28','__restfpr_28')]
    pieces.append(prefix.replace('sub_88044960','NativePrefix',1))
    output.parent.mkdir(parents=True,exist_ok=True)
    result='\n\n'.join(pieces)+'\n'
    output.write_text(result,encoding='utf-8')
    return {'module':module,'generated_sha256':hashlib.sha256(first.encode()).hexdigest(),
            'fixture_sha256':hashlib.sha256(result.encode()).hexdigest(),
            'cut':'880451D4','epilogue':epilogue,'output':str(output)}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module',choices=['CA022100','CA022110'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(prepare(args.module,args.output),indent=2))
