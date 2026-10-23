#!/usr/bin/env python3
"""Annotate agx_sampler PC/LR lines using local PE/PDB files (no downloads).

Usage: agx_sampler_resolve.py sampler.txt module=path.dll[,path.pdb] ...
Percentages and raw addresses are preserved. Export fallback is approximate:
PE exports have no function sizes, so the nearest preceding name is not proof
that an address belongs to that function. Supply matching binaries/PDBs.
"""
import argparse
from bisect import bisect_left, bisect_right
from pathlib import Path
import re
import subprocess
import sys

LLVM = Path('/opt/homebrew/opt/llvm/bin')


def module_key(name):
    name = name.casefold()
    return name[:-4] if name.endswith('.dll') else name


def parse_site(line):
    match = re.match(r'^(?:PC|LR)\s+\d+\s+([^\s+]+)\+0x([0-9a-fA-F]+)(?=\s|$)', line)
    if not match or match[1] == '?':
        return None
    return module_key(match[1]), int(match[2], 16)


def parse_image(text):
    def field(name, default=None):
        match = re.search(r'^\s*' + name + r':\s*(0x[0-9a-fA-F]+|\d+)\s*$', text, re.M)
        if match:
            return int(match[1], 0)
        if default is not None:
            return default
        raise ValueError(f'missing PE {name}')

    base, size = field('ImageBase'), field('SizeOfImage')
    start, length = field('ExportTableRVA', 0), field('ExportTableSize', 0)
    exports = []
    for block in re.findall(r'^Export \{(.*?)^\}', text, re.M | re.S):
        name = re.search(r'^[ \t]*Name:[ \t]*([^\n\r]*)', block, re.M)
        rva = re.search(r'^\s*RVA:\s*(0x[0-9a-fA-F]+|\d+)', block, re.M)
        if not name or not name[1].strip() or not rva:
            continue
        address = int(rva[1], 0)
        # Zero is an absent export; an RVA inside the export directory is a
        # forwarder string, not code in this image.
        if 0 < address < size and not start <= address < start + length:
            exports.append((address, name[1].strip()))
    return base, size, sorted(exports)


def nearest_export(exports, rva):
    index = bisect_right([address for address, _ in exports], rva) - 1
    if index < 0:
        return None
    # Same-RVA aliases use the first name alphabetically (e.g. Nt before Zw).
    index = bisect_left([address for address, _ in exports], exports[index][0])
    address, name = exports[index]
    return f'{name}+0x{rva - address:x} [nearest export]'


def run_llvm(tool, *args):
    return subprocess.run([str(LLVM / tool), *args], check=True,
                          capture_output=True, text=True, timeout=30).stdout


class Image:
    def __init__(self, dll, pdb=None):
        self.dll = str(Path(dll).resolve(strict=True))
        self.pdb = str(Path(pdb).resolve(strict=True)) if pdb else None
        self.base, self.size, self.exports = parse_image(
            run_llvm('llvm-readobj', '--file-headers', '--coff-exports', self.dll))
        self.cache = {}

    def resolve(self, rva):
        if rva in self.cache:
            return self.cache[rva]
        if not 0 <= rva < self.size:
            return '?? [outside image]'
        args = ['--obj', self.dll, '--no-inlines']
        if self.pdb:
            args.append('--pdb=' + self.pdb)
        try:
            output = run_llvm('llvm-symbolizer', *args, hex(self.base + rva))
            lines = output.splitlines()
            symbol = lines[0].strip() if lines else ''
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            print(f'warning: symbolizer failed for {self.dll}: {exc}', file=sys.stderr)
            symbol = ''
        result = symbol if symbol and symbol != '??' else (
            nearest_export(self.exports, rva) or '?? [unresolved]')
        self.cache[rva] = result
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sampler', type=Path)
    parser.add_argument('modules', nargs='+', metavar='module=path.dll[,pdb]')
    args = parser.parse_args(argv)
    try:
        images = {}
        for spec in args.modules:
            name, sep, paths = spec.partition('=')
            if not sep or not name or not paths:
                raise ValueError(f'invalid module mapping: {spec}')
            dll, comma, pdb = paths.partition(',')
            key = module_key(name)
            if key in images:
                raise ValueError(f'duplicate module mapping: {name}')
            images[key] = Image(dll, pdb if comma else None)
        with args.sampler.open() as source:
            for line in source:
                line = line.rstrip('\r\n')
                site = parse_site(line)
                if site and site[0] in images:
                    line += ' SYMBOL ' + images[site[0]].resolve(site[1])
                print(line)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'error: {exc}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
