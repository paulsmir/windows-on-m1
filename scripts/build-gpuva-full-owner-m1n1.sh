#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT_DIRECTORY" >&2
  exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
output=$1
mkdir -p "$output"
output=$(cd "$output" && pwd)

export PATH="/opt/homebrew/bin:/Users/pavel/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH"
export TOOLCHAIN=/opt/homebrew/Cellar/llvm/22.1.8/bin/
export LLDDIR=/tmp/agx-lld-dir/
# Mu with the receipt-sized local reserve (ABI v3, m1n1 62f361bc; Mu 361a2134
# and later) stops in PrePi without it ("reserve receipt v3 required",
# EXP1272). AGX_LOCAL_RESERVE_V2=0 rebuilds the older contract.
reserve=${AGX_LOCAL_RESERVE_V2:-1}

if [ -n "$(/opt/homebrew/bin/git -C "$repo/m1n1_windows" status --porcelain --untracked-files=no)" ]; then
  echo "m1n1 source is dirty" >&2
  exit 1
fi

/Applications/Xcode.app/Contents/Developer/usr/bin/make -C "$repo/m1n1_windows" clean
/Applications/Xcode.app/Contents/Developer/usr/bin/make -C "$repo/m1n1_windows" \
  IOMFB_FULL_OWNER=1 AGX_LOCAL_RESERVE_V2="$reserve" -j4 build/m1n1.macho

cfg="$repo/m1n1_windows/build/build_cfg.h"
if ! grep -qx '#define DCP_IOMFB_FULL_OWNER' "$cfg"; then
  echo "full-owner build flag absent from generated config" >&2
  exit 1
fi
if [ "$reserve" = 1 ] && ! grep -qx '#define AGX_LOCAL_RESERVE_V2' "$cfg"; then
  echo "local reserve v2 flag absent from generated config" >&2
  exit 1
fi

cp "$repo/m1n1_windows/build/m1n1.macho" "$output/m1n1-g2-abi6-iomfb-owner.macho"
python3 - "$repo" "$output" "$reserve" <<'PY'
import hashlib
import json
import subprocess
import sys
from pathlib import Path

repo = Path(sys.argv[1])
output = Path(sys.argv[2])
reserve = int(sys.argv[3])
image = output / 'm1n1-g2-abi6-iomfb-owner.macho'
config = repo / 'm1n1_windows/build/build_cfg.h'
script = repo / 'scripts/build-gpuva-full-owner-m1n1.sh'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

head = subprocess.check_output(
    ['/opt/homebrew/bin/git', '-C', str(repo / 'm1n1_windows'),
     'rev-parse', 'HEAD'], text=True).strip()
data = {
    'schema': 1,
    'm1n1_commit': head,
    'profile': 'J313_GPUVA_FULL_OWNER',
    'make_flags': {'IOMFB_FULL_OWNER': 1, 'AGX_LOCAL_RESERVE_V2': reserve},
    'compiler': '/opt/homebrew/Cellar/llvm/22.1.8/bin/',
    'linker': '/tmp/agx-lld-dir/',
    'generated_config_sha256': sha(config),
    'build_script_sha256': sha(script),
    'macho_sha256': sha(image),
}
(output / 'm1n1-build-info.json').write_text(
    json.dumps(data, sort_keys=True, indent=2) + '\n')
print(json.dumps(data, sort_keys=True))
PY
