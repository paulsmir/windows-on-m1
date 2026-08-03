#!/usr/bin/env python3
"""Generate the m1n1 and Mu J313 guest-layout contracts."""

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from guest_layout import load_layout, render_c, render_dsc, validate_fdf


CONFIG = ROOT / "config/j313-guest-layout.json"
OUTPUTS = {
    ROOT / "m1n1_windows/src/hv_autonomous_layout.generated.h": render_c,
    ROOT / "mu/Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc": render_dsc,
}
FDF = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    layout = load_layout(CONFIG)
    validate_fdf(layout, FDF.read_text())
    stale = []
    for path, renderer in OUTPUTS.items():
        content = renderer(layout)
        if args.check:
            if not path.exists() or path.read_text() != content:
                stale.append(path.relative_to(ROOT))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    if stale:
        print("stale generated guest-layout files:", file=sys.stderr)
        for path in stale:
            print(f"  {path}", file=sys.stderr)
        return 1
    if args.check:
        print("guest layout generated files are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
