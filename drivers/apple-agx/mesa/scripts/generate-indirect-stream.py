#!/usr/bin/env python3
"""Reproduce the RC33 libagx kernel with the pinned local Mesa host tools.

This is a host shader compilation, not a driver/package or hardware build.
The binary is generated for both G13 targets by Mesa's ordinary asahi_clc.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--mesa-source',type=Path,required=True)
    parser.add_argument('--mesa-build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    mesa,build=args.mesa_source.resolve(),args.mesa_build.resolve()
    winsys=Path(__file__).resolve().parents[1]/'winsys'
    source=winsys/'agx_win32_indirect_stream.cl'
    clc=build/'src/compiler/clc/mesa_clc'
    asahi=build/'src/asahi/clc/asahi_clc'
    args.output.mkdir(parents=True,exist_ok=True)
    inputs=[source,winsys/'agx_win32_indirect_stream.h',clc,asahi,
            mesa/'src/asahi/libagx/libagx_dgc.h',build/'src/asahi/genxml/agx_pack.h',
            mesa/'src/asahi/genxml/cmdbuf.xml']
    with tempfile.TemporaryDirectory(prefix='rc33-clc-') as td:
        spv=Path(td)/'stream.spv'
        includes=[winsys,mesa/'include',mesa/'src/compiler/libcl',mesa/'src',
                  mesa/'src/asahi/libagx',build/'src/asahi/genxml']
        cmd=[str(clc),'-o',str(spv),str(source),'--']
        cmd+=['-I'+str(p) for p in includes]
        cmd+=['-cl-std=cl2.0','-D__OPENCL_VERSION__=200','-DNDEBUG',
              '-DHAVE___BUILTIN_FFS','-DHAVE___BUILTIN_CLZ']
        subprocess.run(cmd,check=True)
        h=args.output/'agx_win32_stream_shaders.h'
        c=args.output/'agx_win32_stream_shaders.c'
        subprocess.run([str(asahi),str(spv),str(h),str(c)],check=True)
        manifest={'inputs':{p.name:digest(p) for p in inputs},
                  'outputs':{p.name:digest(p) for p in (h,c)},
                  'spirv_sha256':digest(spv),
                  'targets':['g13g','g13x'],
                  'license':'MIT; original kernel, Mesa encoders/compiler; notices retained'}
        (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__': main()
