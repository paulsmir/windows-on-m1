# Build and packaging

The root repository pins the two source forks required for a matching release. Build from a
recursive clone and do not mix a Python proxyclient from one revision with an m1n1 binary
from another.

## Prerequisites

The supported build host is Apple Silicon macOS with:

- Git and Git submodules;
- Xcode command-line tools and an AArch64-capable LLVM toolchain;
- `make`, `dtc`, Homebrew `rustup`, and the m1n1 host dependencies;
- Homebrew LLVM whose first version line is exactly `Homebrew clang version 22.1.8`;
- Python 3.10 through 3.12 (Homebrew `python@3.12` is the tested choice);
- a running Docker-compatible engine such as Docker Desktop or Colima.

On Apple Silicon macOS, `scripts/build-standalone.sh` uses a hybrid pipeline. Project Mu is
built in the Linux/ARM image because the pinned release does not publish BaseTools for
`MacOs-ARM-64`. Both m1n1 stages are then built natively with the validated Homebrew Clang.
Do not build the whole image inside the container: that selects Ubuntu GCC for m1n1 and has
already produced a target that failed to reconnect after chainload.

Clone all repositories:

```sh
git clone --recurse-submodules https://github.com/paulsmir/windows-on-m1.git
cd windows-on-m1
git submodule update --init --recursive
git submodule status --recursive
```

Create the host-side proxy/KD environment:

```sh
python3 -m venv proxyenv
proxyenv/bin/pip install -r m1n1_windows/requirements.txt
```

Install and initialize the native Rust target used by m1n1:

```sh
brew install llvm rustup python@3.12
export PATH="$(brew --prefix rustup)/bin:$PATH"
rustup default stable
rustup target add aarch64-unknown-none-softfloat
```

For a native Linux build, the script selects a compatible Mu interpreter automatically.
Inspect the selection or override it explicitly when several Python installations are present:

```sh
scripts/build-standalone.sh --check-python
MU_PYTHON=/path/to/python3.12 scripts/build-standalone.sh
```

Python 3.9 and 3.13 are not currently accepted by the native path: the pinned Mu requirements
combine packages that require Python 3.10 or newer with packages that declare support only
through Python 3.12. The container already supplies a compatible interpreter.

## Canonical build

Build the debug artifacts and self-contained image:

```sh
scripts/build-standalone.sh
```

The no-argument image is the production-oriented profile: internal physical display and no
host diagnostics. Other manifest profiles are explicit build options:

```sh
scripts/build-standalone.sh --display physical --debug off
scripts/build-standalone.sh --display both --debug full
scripts/build-standalone.sh --display physical --debug monitor
```

`none`, `physical`, `virtual`, and `both` are valid display values. `off`, `uart`, `full`, and
`monitor` are valid debug values. A virtual display still requires a USB host-side consumer;
a normal standalone power-on should use `physical --debug off`. `monitor` is a diagnostic
profile that exposes console/vUART while always continuing into Windows instead of allowing
proxy takeover.

Each invocation replaces `dist/j313/boot.bin`. Preserve named copies before building the next
profile so a known-good diagnostic image cannot be confused with the quiet production image:

```sh
mkdir -p .local/validated-artifacts

scripts/build-standalone.sh --display physical --debug monitor
cp dist/j313/boot.bin .local/validated-artifacts/boot-physical-monitor.bin
shasum -a 256 .local/validated-artifacts/boot-physical-monitor.bin

scripts/build-standalone.sh --display physical --debug off
cp dist/j313/boot.bin .local/validated-artifacts/boot-physical-production.bin
shasum -a 256 .local/validated-artifacts/boot-physical-production.bin
```

The production profile is the only normal-use and performance profile. The monitor image keeps
USB console endpoints active and may emit verbose synchronous USB logging; it is deliberately
observable, not deliberately low overhead.

`STANDALONE_BUILD_CONTAINER=auto` is the supported macOS mode. It runs only the Mu portion in
the container and returns to macOS for both m1n1 stages. `never` requires native Project Mu
BaseTools, which the pinned tree does not provide for Apple Silicon macOS. `always` is a
development escape hatch and is not a qualified release path.

For release optimization:

```sh
scripts/build-standalone.sh --release
```

The script:

1. prepares a private Mu virtual environment under `.build/`;
2. builds the J313 Mu firmware with the virtual GIC backend;
3. verifies the native m1n1 compiler identity;
4. builds distinct bootstrap-only stage-0 and hypervisor stage-1 binaries and runs host tests;
5. verifies that generated layout constants match `config/j313-guest-layout.json`;
6. packs the compressed Mu FD and versioned manifest;
7. writes `boot.bin`, both stage binaries, `m1n1.macho`, `J313_EFI.fd`, `SHA256SUMS`, and
   `BUILD-METADATA.json` under `dist/j313/`.

`BUILD-METADATA.json` records the exact m1n1 source commit, compiler identity, image hash,
and separate role/hash/size records. Packaging aborts if the stage hashes are identical.

Review the commands without changing the tree:

```sh
BUILD_STANDALONE_DRY_RUN=1 scripts/build-standalone.sh
```

## Assisted-development artifacts

The assisted path uses the same source revisions and guest layout:

```sh
scripts/build-development.sh
```

This produces:

- `dist/j313/m1n1.macho` for `chainload.py`;
- `dist/j313/J313_EFI.fd` for `run_uefi.py`;
- the standalone `dist/j313/boot.bin` from the same components.

Keeping these together prevents the `Bad Command` failure caused by running a newly built
Python proxy against an older target binary that lacks its proxy opcode.

## Layout contract

`config/j313-guest-layout.json` is the single source of truth for pinned guest RAM, firmware,
low-memory alias, RAM disk, virtual framebuffer, ECAM, BAR, and interrupt values. Regenerate
or check derived Python, C, and Mu declarations with:

```sh
python3 tools/generate_guest_layout.py --check
```

Do not change one generated copy by hand. A mismatch can make UEFI write outside its reserved
memory or make Windows DMA target the wrong physical pages.

## Packing an existing build

The packer can be run explicitly:

```sh
python3 tools/pack_boot.py \
  --m1n1 m1n1_windows/build/m1n1.bin \
  --firmware mu/Build/MacBookAirMid2020-AARCH64/DEBUG_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd \
  --layout config/j313-guest-layout.json \
  --output dist/j313/boot.bin \
  --display physical \
  --debug off
```

It validates alignment, offsets, decompressed size, layout version, CRC32, and launch-profile
flags. The native m1n1 parser repeats those checks before touching guest memory. The profile
is carried by `boot.bin`; there is no second configuration file on the ESP.

Inspect a preserved image before installation:

```sh
python3 -c 'from pathlib import Path; from bootstrap_image import parse_bootstrap; from standalone_image import parse_image; outer, inner = parse_bootstrap(Path(".local/validated-artifacts/boot-physical-production.bin").read_bytes()); nested, firmware = parse_image(inner); assert outer.flags == nested.flags; print(outer); print(nested); print(f"firmware={len(firmware)}")'
```

The production copy must report `display=physical`, `debug=off`; the monitor copy must report
`display=physical`, `debug=monitor`. Reject an image whose parser and filename disagree.

The monitor flag is manifest ABI value `0x10`; combined with physical display it produces flags
`0x11`. Do not append a monitor manifest to an older `m1n1.bin`: exact decoding intentionally
rejects unknown or combined debug values. Always build and install `boot.bin` as one artifact
from a single recursive checkout.

## Verification

Run the root tests:

```sh
proxyenv/bin/python -m unittest discover -s tests -v
```

Run m1n1 host tests:

```sh
m1n1_windows/tests/run_host_tests.sh
```

Compare release hashes:

```sh
cd dist/j313
shasum -a 256 -c SHA256SUMS
```

Mu artifacts may be absent in a source-only checkout; tests that inspect the compiled FD are
expected to skip until Mu has been built.
