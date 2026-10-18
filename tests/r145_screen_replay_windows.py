"""Extract production screen lifecycle bodies for a pinned-WDK callback replay."""
from pathlib import Path
import argparse
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('AdmissionUmdScreenFind', 'AdmissionUmdScreenFreeSlot',
         'AdmissionUmdScreenClass', 'AdmissionUmdScreenCreateClassBufferImpl',
         'AdmissionUmdScreenCreateClassBuffer', 'AdmissionUmdScreenAdoptAllocation',
         'AdmissionUmdScreenCpuAllocation', 'AdmissionUmdScreenMapBuffer', 'AdmissionUmdScreenUnmapBuffer',
         'AdmissionUmdScreenDestroyBuffer')

def generate(output):
    source = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c').read_text()
    parts = []
    for name in NAMES:
        if name == 'AdmissionUmdScreenCreateClassBufferImpl' and name not in source:
            continue
        parts.append(body(source, name))
    Path(output).write_text('\n\n'.join(parts))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    generate(parser.parse_args().output)
