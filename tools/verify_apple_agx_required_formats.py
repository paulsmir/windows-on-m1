#!/usr/bin/env python3
"""Validate the frozen FL10_0 required-format contract and evidence."""
from __future__ import annotations
import argparse,json,sys
from pathlib import Path

def expected_formats():
    values={'DXGI_FORMAT_UNKNOWN'}
    groups={
      'R32G32B32A32':['TYPELESS','FLOAT','UINT','SINT'],
      'R32G32B32':['TYPELESS','FLOAT','UINT','SINT'],
      'R16G16B16A16':['TYPELESS','FLOAT','UNORM','UINT','SNORM','SINT'],
      'R32G32':['TYPELESS','FLOAT','UINT','SINT'],
      'R8G8B8A8':['TYPELESS','UNORM','UNORM_SRGB','UINT','SNORM','SINT'],
      'R16G16':['TYPELESS','FLOAT','UNORM','UINT','SNORM','SINT'],
      'R32':['TYPELESS','FLOAT','UINT','SINT'],
      'R8G8':['TYPELESS','UNORM','UINT','SNORM','SINT'],
      'R16':['TYPELESS','FLOAT','UNORM','UINT','SNORM','SINT'],
      'R8':['TYPELESS','UNORM','UINT','SNORM','SINT'],
      'R10G10B10A2':['TYPELESS','UNORM','UINT'],
    }
    for stem,types in groups.items(): values.update('DXGI_FORMAT_'+stem+'_'+t for t in types)
    values.add('DXGI_FORMAT_R11G11B10_FLOAT')
    values.update('DXGI_FORMAT_'+x for x in [
      'R32G8X24_TYPELESS','D32_FLOAT_S8X24_UINT','R32_FLOAT_X8X24_TYPELESS','X32_TYPELESS_G8X24_UINT',
      'D32_FLOAT','D24_UNORM_S8_UINT','R24G8_TYPELESS','R24_UNORM_X8_TYPELESS','X24_TYPELESS_G8_UINT','D16_UNORM'])
    for i,types in [(1,['TYPELESS','UNORM','UNORM_SRGB']),(2,['TYPELESS','UNORM','UNORM_SRGB']),
                    (3,['TYPELESS','UNORM','UNORM_SRGB']),(4,['TYPELESS','UNORM','SNORM']),
                    (5,['TYPELESS','UNORM','SNORM'])]:
      values.update(f'DXGI_FORMAT_BC{i}_{t}' for t in types)
    values.update('DXGI_FORMAT_'+x for x in [
      'A8_UNORM','R9G9B9E5_SHAREDEXP','R8G8_B8G8_UNORM','G8R8_G8B8_UNORM',
      'B5G6R5_UNORM','B5G5R5A1_UNORM','B4G4R4A4_UNORM'])
    for stem in ['B8G8R8A8','B8G8R8X8']:
      values.update(f'DXGI_FORMAT_{stem}_{t}' for t in ['TYPELESS','UNORM','UNORM_SRGB'])
    return values

ALLOWED_USES={'buffer-storage','cpu-access','storage','compatible-views','texture','typed-buffer',
 'vertex','stream-output','srv','rtv','filter','blend','depth-stencil'}
LOWERED={'DXGI_FORMAT_A8_UNORM','DXGI_FORMAT_R32G32B32_TYPELESS','DXGI_FORMAT_R32G32B32_FLOAT',
 'DXGI_FORMAT_R32G32B32_UINT','DXGI_FORMAT_R32G32B32_SINT','DXGI_FORMAT_R8G8_B8G8_UNORM',
 'DXGI_FORMAT_G8R8_G8B8_UNORM'}

def validate(value,root:Path):
    if value.get('schema_version')!=1 or value.get('feature_level')!='D3D_FEATURE_LEVEL_10_0' or value.get('ddi')!='D3D10_0_DDI_INTERFACE_VERSION': raise ValueError('identity')
    if value.get('optional_msaa') is not False or value.get('optional_xr_bias') is not False: raise ValueError('optional policy')
    entries=value.get('entries')
    if not isinstance(entries,list): raise ValueError('entries')
    names=[e.get('format') for e in entries]
    if len(names)!=len(set(names)): raise ValueError('duplicate format')
    expected=expected_formats()
    if set(names)!=expected: raise ValueError(f'format set missing={sorted(expected-set(names))} extra={sorted(set(names)-expected)}')
    lock=json.loads((root/'drivers/apple-agx/mesa/mesa-source.lock.json').read_text())
    if value.get('mesa_commit')!=lock.get('commit'): raise ValueError('mesa commit')
    if not isinstance(value.get('mapping_source'),str) or not value['mapping_source']: raise ValueError('mapping source')
    observed_lowered=set()
    for e in entries:
      for key in ('families','representations','required_uses','evidence'):
        if not isinstance(e.get(key),list) or not e[key]: raise ValueError(f'{e.get("format")} {key}')
      if not set(e['required_uses'])<=ALLOWED_USES: raise ValueError(f'{e["format"]} use')
      if any('lowered' in r for r in e['representations']): observed_lowered.add(e['format'])
      for evidence in e['evidence']:
        if not (root/evidence).is_dir(): raise ValueError(f'{e["format"]} evidence')
    if observed_lowered!=LOWERED: raise ValueError('lowered set')
    sub=value.get('subresource_evidence')
    if not isinstance(sub,str) or not (root/sub).is_dir(): raise ValueError('subresource evidence')
    return {'formats':len(entries),'lowered':len(observed_lowered),'optional_msaa':False,'optional_xr_bias':False}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--contract',type=Path,required=True);ap.add_argument('--root',type=Path,required=True);a=ap.parse_args()
    try:
      result=validate(json.loads(a.contract.read_text()),a.root)
      print(json.dumps(result,sort_keys=True,separators=(',',':')));return 0
    except (OSError,ValueError,TypeError,json.JSONDecodeError) as e:
      print(f'required format contract error: {e}',file=sys.stderr);return 1
if __name__=='__main__': raise SystemExit(main())
