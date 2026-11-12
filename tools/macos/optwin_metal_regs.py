#!/usr/bin/env python3
"""Highest register and threads per core of each OPTWIN Metal pipeline (the macOS side of OPTWIN_SHADER).

Usage: optwin_metal_regs.py DIR_OR_METALLIB... [--oracle .local/metal-oracle] [--applegpu .local/reference/applegpu]

Input: the files of `optwin_metal --archive-each DIR` (one pipeline per MTLBinaryArchive, named
optwin_<kernel>_<NTX>x<NTY>_alt<ALT>.metallib) made on the J313 (G13; another GPU's code does not decode).
Each archive is split at the Mach-O magic CF FA ED FE into slices (slice 0 is the archive index, the
others carry __text); each __text is disassembled with the applegpu disassembler exactly as
.local/metal-oracle/dis.py does, and the highest rN / rNl / rNh operand gives the half-register count,
which Asahi's agx_performance.c occupancy table maps to threads per core (the same quantity as the
D3D12 side's measure-d3d12-shader-stats threads). Prints one OPTWIN_SHADER_METAL line per code slice;
failed= counts undecodable instructions (non-zero = wrong ISA or a disassembler gap: do not trust).
"""
import argparse
import contextlib
import io
import os
import re
import struct
import sys

MAGIC = b'\xcf\xfa\xed\xfe'
# Asahi src/asahi/compiler/agx_performance.c occupancies[]: (max half registers, threads per core)
OCCUPANCY = [(104, 1024), (112, 896), (128, 832), (136, 768), (144, 704),
             (160, 640), (184, 576), (208, 512), (232, 448), (256, 384)]
REG = re.compile(r'(?<![A-Za-z0-9])r(\d+)([lh]?)(?![A-Za-z0-9])')


def threads_per_core(halfregs):
    for limit, threads in OCCUPANCY:
        if halfregs <= limit:
            return threads
    return 0


def halfregs_of(text):
    """Half registers spanned by the highest register operand of a disassembly listing."""
    high = 0
    for line in text.splitlines():
        operands = line.split(None, 2)[2] if len(line.split(None, 2)) > 2 else ''
        for number, half in REG.findall(operands):
            n = int(number)
            high = max(high, 2 * n + (1 if half == 'l' else 2))
    return high


def slices(blob):
    offs = [m.start() for m in re.finditer(re.escape(MAGIC), blob)] + [len(blob)]
    return [blob[offs[i]:offs[i + 1]] for i in range(len(offs) - 1)]


def text_section(macho, part):
    secs, _ = macho.parse(part)
    text = [s for s in secs if s[1] == '__text']
    return part[text[0][4]:text[0][4] + text[0][3]] if text else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('paths', nargs='+')
    ap.add_argument('--oracle', default='/Volumes/pwdev/public_windows/.local/metal-oracle')
    ap.add_argument('--applegpu', default='/Volumes/pwdev/public_windows/.local/reference/applegpu')
    args = ap.parse_args(argv)
    sys.path[:0] = [args.applegpu, args.oracle]
    import disassemble  # applegpu
    import macho  # .local/metal-oracle/macho.py
    disassemble.STOP_ON_STOP = False
    files = []
    for p in args.paths:
        files += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith('.metallib')) if os.path.isdir(p) else [p]
    for path in files:
        blob = open(path, 'rb').read()
        for index, part in enumerate(slices(blob)):
            try:
                code = text_section(macho, part)
            except (struct.error, ValueError, IndexError):
                code = None
            if not code:
                continue
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out):
                    disassemble.disassemble(code)
            except Exception as error:  # another GPU's ISA trips the decoder's asserts
                print(f'OPTWIN_SHADER_METAL file={os.path.basename(path)} slice={index} error={type(error).__name__}')
                continue
            listing = out.getvalue()
            half = halfregs_of(listing)
            print(f'OPTWIN_SHADER_METAL file={os.path.basename(path)} slice={index} '
                  f'max_reg={"r%d" % ((half - 1) // 2) if half else "none"} '
                  f'regs32={(half + 1) // 2} halfregs={half} threads_per_core={threads_per_core(half)} '
                  f'instrs={sum(1 for l in listing.splitlines() if re.match(r"\s*[0-9a-f]+:", l))} '
                  f'failed={listing.count("disassembly failed")}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
