#!/usr/bin/env python3
"""Compile the original timestamp reduction with pinned local Mesa host tools.

This compiles an OpenCL shader for both G13 targets, not a driver or package.
All source, tool and output hashes are recorded for offline reproduction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


LICENSE = """The original timestamp kernel and generated bundle are MIT licensed.
The Mesa compiler's generated-file notice and shared-header notices follow:

Copyright The Asahi Linux Contributors
Copyright 2024 Valve Corporation
Copyright 2023 Alyssa Rosenzweig
Copyright 2014 Intel Corporation

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--mesa-source', type=Path, required=True)
    parser.add_argument('--mesa-build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    mesa, build = args.mesa_source.resolve(), args.mesa_build.resolve()
    winsys = Path(__file__).resolve().parents[1] / 'winsys'
    source = winsys / 'agx_win32_query_timestamp.cl'
    clc = build / 'src/compiler/clc/mesa_clc'
    asahi = build / 'src/asahi/clc/asahi_clc'
    inputs = [source, winsys / 'agx_win32_query_timestamp.h', Path(__file__),
              clc, asahi, mesa / 'src/compiler/libcl/libcl.h',
              mesa / 'src/compiler/libcl/stdint.h', mesa / 'src/util/macros.h']
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='agx-query-timestamp-') as td:
        spv = Path(td) / 'timestamp.spv'
        includes = [winsys, mesa / 'include', mesa / 'src/compiler/libcl', mesa / 'src']
        command = [str(clc), '-o', str(spv), str(source), '--']
        command += ['-I' + str(path) for path in includes]
        command += ['-cl-std=cl2.0', '-D__OPENCL_VERSION__=200', '-DNDEBUG',
                    '-DHAVE___BUILTIN_FFS', '-DHAVE___BUILTIN_CLZ']
        subprocess.run(command, check=True)
        header = args.output / 'agx_win32_query_timestamp_shaders.h'
        library = args.output / 'agx_win32_query_timestamp_shaders.c'
        subprocess.run([str(asahi), str(spv), str(header), str(library)], check=True)
        for generated in (header, library):
            generated.write_bytes(generated.read_bytes().rstrip() + b'\n')
        notice = args.output / 'LICENSE.txt'
        notice.write_text(LICENSE)
        manifest = {
            'inputs': {path.name: digest(path) for path in inputs},
            'outputs': {path.name: digest(path) for path in (header, library, notice)},
            'spirv_sha256': digest(spv),
            'targets': ['g13g', 'g13x'],
            'license': 'MIT; original kernel, Mesa compiler; notices retained',
        }
        (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
