# Running Windows

This project has two intentional modes. Standalone mode is the final user-facing boot path.
Assisted development mode remains a first-class path for changing m1n1, Mu, ACPI, device
models, and Windows-facing firmware while observing the target from another Mac.
The second Mac is optional: it is only required for assisted development and debugging, not
for normal standalone Windows boot.

## Standalone mode

After [installation](INSTALL.md), select the Asahi-provisioned boot entry in Apple startup
options. The expected chain is:

```text
iBoot
  -> Asahi-authorized boot entry
  -> <ESP>/m1n1/boot.bin
  -> native m1n1 EL2 guest setup
  -> embedded J313 Mu firmware
  -> internal \EFI\BOOT\BOOTAA64.EFI
  -> Windows 11 ARM64
```

The default packed image (`display=physical`, `debug=off`) does not initialize the USB debug
transport and does not wait for a maintenance window. m1n1 prepares the internal DCP surface
and starts the embedded firmware immediately. A second computer is not consulted during this
normal path.

Images explicitly built with `debug=uart`, `debug=full`, or a virtual display open the USB
transport and retain the bounded proxy window. If a debug host opens the proxy during that
window, m1n1 transfers control to the proxy loop instead of starting Windows. This provides a
recovery route for chainloading another build without writing the ESP again.

Status: the self-contained path is implemented and host-tested; hardware validation pending
for the current packed image. Do not interpret build or parser tests as proof of cold boot.

## Assisted development mode

Assisted mode uses another Apple Silicon Mac, referred to as the host. The M1 Air is the
target. USB transports two ACM endpoints: the m1n1 proxy and the guest virtual UART.

This path is used for:

- early m1n1 and Mu logs;
- the native 2560x1600 virtual framebuffer;
- testing replacement m1n1 or Mu builds without rewriting the Air ESP;
- Windows KD and PnP/ACPI/storage diagnostics;
- hang telemetry and framebuffer/proxy backpressure analysis.

Prepare the host environment once after cloning:

```sh
python3 -m venv proxyenv
proxyenv/bin/pip install -r m1n1_windows/requirements.txt
```

### 1. Enter proxy mode

Physically reboot the target and connect the host before the standalone three-second window
expires. Alternatively install or restore a proxy-only m1n1 payload from macOS on the target.

List the endpoints on the host:

```sh
ls -l /dev/cu.usbmodem*
```

With exactly two project endpoints, `scripts/run-assisted.sh` can select them in lexical
order. If any other modem devices are present, pass both explicitly.

### 2. Chainload matching m1n1 and launch Windows

Stop if another `run_uefi.py` owns the proxy. The recommended single command chainloads the
matching m1n1, waits for re-enumeration, and starts Mu/Windows:

```sh
scripts/run-windows.sh \
  --execution assisted \
  --display both \
  --debug full \
  --chainload \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART
```

Use `--m1n1 path/to/m1n1.macho` or `--firmware path/to/J313_EFI.fd` to replace one component.
Internally, `--chainload` invokes
`m1n1_windows/proxyclient/tools/chainload.py`; normally use the wrapper so endpoint discovery
and the reconnect wait remain consistent.
A `Bad Command` at the first PCI or framebuffer operation normally means the chainload did not
happen or used a different build.

### 3. Start log and framebuffer viewers

```sh
scripts/log-assisted.sh
scripts/display-assisted.sh
```

The live hypervisor log is served at `http://127.0.0.1:8765/`. The virtual framebuffer is
served at `http://127.0.0.1:8766/`. Both servers reconnect to new files across guest runs.

### 4. Start Mu and Windows without another chainload

Review the exact command first:

```sh
scripts/run-assisted.sh --dry-run \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART \
  --firmware dist/j313/J313_EFI.fd
```

Then launch:

```sh
scripts/run-assisted.sh \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART \
  --firmware dist/j313/J313_EFI.fd
```

This lower-level form assumes matching m1n1 is already running. For the usual development
cycle, prefer the `run-windows.sh ... --chainload` command above.

For WinPE experiments that intentionally preload an image into guest RAM, add
`--ramdisk path/to/winpe.img`. Installed Windows normally boots from internal NVMe and does
not need a RAM disk.

The string `reader-before-guest` in dry-run output describes a correctness requirement, not
cosmetic ordering. Mu prints during PrePi; m1n1 drops virtual-UART bytes when the host endpoint
is not open, so the reader must start before `run_uefi.py` enters the guest.

Only one process may own the proxy. Never start a second `run_uefi.py` and never kill the
host process while assuming that the EL2 guest also stopped.

### 5. Observe the run

Important files are:

- `hv.log` — proxyclient and EL2/hypervisor log;
- `guest-uart.log` — raw Mu/Windows serial data;
- `guest-uart.tlog` — the same guest UART with host-relative timestamps;
- `fb.raw` and `fb-info.json` — last complete virtual framebuffer publication;
- `hang-telemetry-status.json` — observer-side diagnostic status.

A stale frame alone does not prove Windows stopped. Consult [DEBUGGING.md](DEBUGGING.md).

### 6. Reset safely

If the UEFI shell is active:

```sh
scripts/reset-assisted.sh --proxy /dev/cu.PROXY --vuart /dev/cu.VUART
```

If Windows and KD respond, prefer:

```sh
M1N1VUART=/dev/cu.VUART proxyenv/bin/python tools/kd/kd_reboot.py
```

Do not use `pkill run_uefi.py` as a reset mechanism. The guest can continue running in EL2
after its host-side proxy loop disappears, leaving the USB protocol desynchronized until a
physical reboot.

## Transferring a standalone image by SSH

SSH is not part of either boot chain. It is merely a convenient way to copy `boot.bin` and
the repository to the Air while the Air is running macOS:

```sh
scp dist/j313/boot.bin air-host:~/boot.bin
```

The ESP replacement itself must run locally on the Air with `sudo` because it mounts and
writes that machine's ESP. This transfer-and-install step is needed only when changing the
standalone image or performing its hardware smoke test.
