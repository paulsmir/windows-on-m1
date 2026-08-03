# Build and packaging

The root repository pins the two source forks required for a matching release. Build from a
recursive clone and do not mix a Python proxyclient from one revision with an m1n1 binary
from another.

## Prerequisites

The supported build host is Apple Silicon macOS with:

- Git and Git submodules;
- Xcode command-line tools and an AArch64-capable LLVM toolchain;
- `make`, `dtc`, Rust, and the m1n1 host dependencies;
- a running Docker-compatible engine such as Docker Desktop or Colima.

On Apple Silicon macOS, `scripts/build-standalone.sh` automatically builds and enters the
Linux/ARM image from `Dockerfile.build`. This is required because the pinned Project Mu
release does not publish its prebuilt BaseTools for `MacOs-ARM-64`. Native Linux/ARM builds
use Python 3.10 through 3.12 with `venv` and run the same inner pipeline directly.

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

Set `STANDALONE_BUILD_CONTAINER=always` to use the container on another host, or `never` only
when a complete native Project Mu BaseTools environment is already available.

For release optimization:

```sh
scripts/build-standalone.sh --release
```

The script:

1. prepares a private Mu virtual environment under `.build/`;
2. builds the J313 Mu firmware with the virtual GIC backend;
3. builds m1n1 and runs its host tests;
4. verifies that generated layout constants match `config/j313-guest-layout.json`;
5. packs the compressed Mu FD and versioned manifest after `m1n1.bin`;
6. writes `dist/j313/boot.bin`, `m1n1.macho`, `J313_EFI.fd`, and `SHA256SUMS`.

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
  --output dist/j313/boot.bin
```

It validates alignment, offsets, decompressed size, layout version, and CRC32. The native
m1n1 parser repeats those checks before touching guest memory.

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
