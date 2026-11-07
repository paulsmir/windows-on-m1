#!/usr/bin/env python3
"""
Boot the Project Mu UEFI firmware as an m1n1 hypervisor guest at a FIXED address.

Why: PrePi.c uses the hardcoded PcdFdBaseAddress, so the image must be built for
the exact address it is loaded at. run_guest.py derives that address from
u.heap_top, which moves whenever m1n1 itself is loaded at a different base --
i.e. on every boot. Pinning heap_top makes the load address reproducible, so the
firmware can be built to match it.

  ./run_uefi.py --dry-run          validate and print the virtual display contract
  ./run_uefi.py firmware.fd        load and run

PHYS_BASE anchors the fixed FD; --guest-ram-profile expanded lowers RAM independently.
"""
import hashlib, os, sys, pathlib, argparse, signal

from guest_layout import DEFAULT_LAYOUT_PATH, load_layout
from launch_profile import parse_profile
from virtual_display import FrameReceiver, VirtualDisplayConfig
from hang_telemetry import TelemetryRecorder

ap = argparse.ArgumentParser()
ap.add_argument("payload", nargs="?", type=pathlib.Path)
ap.add_argument("--dry-run", action="store_true",
                help="validate and print the virtual framebuffer contract without USB")
ap.add_argument("--storage-mode", choices=("VNVME_RECOVERY", "ANS_NATIVE"),
                default="VNVME_RECOVERY", help="explicit storage ownership profile")
ap.add_argument("--ans-direct", action="store_true",
                help="ANS_NATIVE only: the EBS handoff cold-boots ANS to the Asahi hand-off state and "
                     "passes the controller to the guest driver (no EL2 broker, no EL2 write window); "
                     "exclusive with a manifest write window")
ap.add_argument("--qualification-manifest", type=pathlib.Path)
ap.add_argument("--qualification-sha256")
ap.add_argument("--qualified-m1n1", type=pathlib.Path)
ap.add_argument("--managed-vuart", help="runner-owned recovery UART reader endpoint")
ap.add_argument("--qualified-vuart", help="native UART reader endpoint, started only after chainload")
from tools.ans_qualification import PROFILES as ANS_PROFILES
ap.add_argument("--confirm-experiment", choices=tuple(ANS_PROFILES))
ap.add_argument("--require-execution-confirmation", action="store_true",
                help="require execution confirmation during shell's hardware-free preflight")
ap.add_argument("--layout", type=pathlib.Path, default=DEFAULT_LAYOUT_PATH,
                help="canonical J313 guest-layout JSON")
ap.add_argument("--device", default=os.environ.get("M1N1DEVICE"))
ap.add_argument("--display-mode", choices=("none", "physical", "virtual", "both"),
                default="virtual", help="guest framebuffer consumers")
ap.add_argument("--debug-mode", choices=("off", "uart", "full", "monitor"), default="uart",
                help="diagnostic work performed beside the guest")
ap.add_argument("--phys-base", type=lambda x: int(x, 0))
ap.add_argument("--guest-ram-profile", choices=("legacy", "expanded"), default="legacy",
                help="expanded: keep the fixed Mu image, use a 64 MiB proxy heap, and "
                     "expose RAM from 0x818000000 after live heap/ADT validation")
ap.add_argument("--ramdisk", type=pathlib.Path,
                help="disk image to preload into guest RAM at --ramdisk-base. Anything past a "
                     "few tens of MB cannot ride inside the firmware volume: FvMain is "
                     "decompressed whole during PrePi and dies there with Out of Resources.")
ap.add_argument("--ramdisk-base", type=lambda x: int(x, 0),
                help="must match PcdPreloadedRamdiskBase in T810XFamilyPkg.dsc.inc")
ap.add_argument("--ramdisk-max-size", type=lambda x: int(x, 0),
                help="must match PcdPreloadedRamdiskMaxSize")
ap.add_argument("--low-mem", action="store_true",
                help="alias a window of DRAM to a low guest-physical address. The Windows boot "
                     "manager asks for pages down there; Apple DRAM starts at 0x800000000. "
                     "Must match PcdLowMemoryWindow* in T810XFamilyPkg.dsc.inc.")
ap.add_argument("--low-mem-base", type=lambda x: int(x, 0))
ap.add_argument("--low-mem-size", type=lambda x: int(x, 0))
ap.add_argument("--low-mem-backing", type=lambda x: int(x, 0))
ap.add_argument("--fb-base", type=lambda x: int(x, 0),
                help="reserved guest framebuffer address")
ap.add_argument("--fb-width", type=int)
ap.add_argument("--fb-height", type=int)
ap.add_argument("--guest-ram-end", type=lambda x: int(x, 0),
                help="conservative RAM end used only by hardware-free preflight validation")
ap.add_argument("--no-pin", action="store_true",
                help="use m1n1's natural heap_top instead of a pinned base. Needed because "
                     "PcdBootArgsPointer/PcdAdtPointer are hardcoded to 0x840000000/0x840004000, "
                     "which only falls inside guest RAM with the natural layout.")
ap.add_argument("--wdt-cpu", type=int, choices=range(8), default=None,
                help="reserve one physical CPU for the hypervisor watchdog (0..7). Omit this "
                     "for an eight-core guest; a reserved CPU must not also be enabled in MADT.")
ap.add_argument("--snapshot-kernel-read", metavar="RVA:LEN",
                help="read-only diagnostic: on each SIGINT snapshot, print LEN bytes at ntoskrnl+RVA "
                     "(kernel base found from the interrupted guest PC), e.g. KiBugCheckData")
ap.add_argument("--snapshot-devnodes", metavar="SPEC",
                help="read-only diagnostic: on each SIGINT snapshot, walk the kernel PnP tree "
                     "(ROOT_RVA,child,sibling,instance,service,state,problem,problem_status)")
ap.add_argument("--snapshot-modules", metavar="SPEC",
                help="read-only diagnostic: on each SIGINT snapshot, list kernel loaded modules "
                     "(PsLoadedModuleList RVA,BaseDllName offset,Flags offset)")
ap.add_argument("--contract-output", type=pathlib.Path,
                help="write four CRC-framed launch-contract checkpoints")
args = ap.parse_args()
# Ambient environment cannot opt an ordinary launch into native ANS ownership.
os.environ["WOM1_STORAGE_MODE"] = args.storage_mode
os.environ.pop("WOM1_ANS_WRITE_WINDOW", None)
os.environ.pop("WOM1_ANS_DIRECT", None)
qualification_paths = None
ans_write_window = None
if args.storage_mode == "ANS_NATIVE":
    from tools.ans_qualification import validate_launch, write_window_env, direct_mode, OVERRIDES
    root = pathlib.Path(__file__).resolve().parent
    try:
        qualification_manifest, qualification_paths = validate_launch(
            args.qualification_manifest, args.qualification_sha256, root,
            firmware=args.payload, m1n1=args.qualified_m1n1, ramdisk=args.ramdisk,
            layout=args.layout, display=args.display_mode, debug=args.debug_mode,
            low_mem=args.low_mem, no_pin=args.no_pin, wdt_cpu=args.wdt_cpu,
            overrides=[name for name in OVERRIDES if getattr(args, name) is not None],
            confirmation=args.confirm_experiment, contract_output=args.contract_output,
            execute=(not args.dry_run or args.require_execution_confirmation))
        args.payload = qualification_paths['mu']
        args.qualified_m1n1 = qualification_paths['m1n1']
        args.ramdisk = qualification_paths['winpe']
        # ANS008: only a sealed manifest can open the EL2 WRITE window.
        ans_write_window = write_window_env(qualification_manifest)
        if args.ans_direct and ans_write_window is not None:
            raise ValueError("--ans-direct is exclusive with the manifest EL2 write window")
        if args.ans_direct != direct_mode(qualification_manifest):
            raise ValueError("--ans-direct must match the sealed manifest's direct profile")
        if ans_write_window is not None:
            os.environ["WOM1_ANS_WRITE_WINDOW"] = ans_write_window
        # ANS-DIRECT: set only after admission, like the window (no ambient opt-in).
        if args.ans_direct:
            os.environ["WOM1_ANS_DIRECT"] = "1"
    except (ValueError, OSError, TypeError) as error:
        ap.error(str(error))
elif args.ans_direct:
    ap.error("--ans-direct requires explicit --storage-mode ANS_NATIVE")
elif any((args.qualification_manifest, args.qualification_sha256, args.qualified_m1n1,
          args.confirm_experiment, args.require_execution_confirmation, args.qualified_vuart)):
    ap.error("qualification arguments require explicit --storage-mode ANS_NATIVE")

profile = parse_profile("assisted", args.display_mode, args.debug_mode)

layout = load_layout(args.layout)
defaults = {
    "phys_base": layout.phys_base,
    "ramdisk_base": layout.ramdisk_base,
    "ramdisk_max_size": layout.ramdisk_max_size,
    "low_mem_base": layout.low_mem_ipa,
    "low_mem_size": layout.low_mem_size,
    "low_mem_backing": layout.low_mem_pa,
    "fb_base": layout.virtual_fb_base,
    "fb_width": layout.virtual_fb_width,
    "fb_height": layout.virtual_fb_height,
    "guest_ram_end": layout.ram_end,
}
for name, value in defaults.items():
    if getattr(args, name) is None:
        setattr(args, name, value)

expanded_ram = args.guest_ram_profile == "expanded"
if expanded_ram and (args.no_pin or args.phys_base != layout.phys_base):
    ap.error("expanded RAM requires the canonical pinned firmware layout")
if expanded_ram and os.environ.get("M1N1HEAP"):
    ap.error("expanded RAM does not support M1N1HEAP overrides")

fb_stride = layout.virtual_fb_stride
if args.fb_width != layout.virtual_fb_width:
    fb_stride = args.fb_width * 4
fb = VirtualDisplayConfig(
    base=args.fb_base,
    width=args.fb_width,
    height=args.fb_height,
    stride=fb_stride,
)
preflight_windows = {
    "ramdisk": (args.ramdisk_base, args.ramdisk_base + args.ramdisk_max_size),
}
if args.low_mem:
    preflight_windows["low-memory backing"] = (
        args.low_mem_backing,
        args.low_mem_backing + args.low_mem_size,
    )
fb.validate((args.phys_base, args.guest_ram_end), preflight_windows)

print("=" * 60)
print(f"virtual framebuffer       : 0x{fb.base:x}..0x{fb.end:x}")
print(f"geometry / stride / size  : {fb.width}x{fb.height} / {fb.stride} / 0x{fb.size:x}")
print(f"storage mode             : {args.storage_mode}")
if args.storage_mode == "ANS_NATIVE":
    if args.ans_direct:
        print("ANS ownership            : direct (guest driver owns the controller after EBS)")
    else:
        print(f"ANS write window         : {ans_write_window or 'none (read-only)'}")
print(f"display mode             : {profile.display.value}")
print(f"debug mode               : {profile.debug.value}")
print(f"physical DCP             : {'enabled' if profile.physical_display else 'disabled'}")
print(f"USB framebuffer          : {'enabled' if profile.virtual_display else 'disabled'}")
print(f"telemetry                : {'enabled' if profile.telemetry else 'disabled'}")
print("preflight validation      : OK")
print("=" * 60)

# A dry run is deliberately hardware-free. Exact firmware placement is checked again after
# load_raw(), when the real ADT/TrustCache sizes and physical RAM end are known.
if args.dry_run or args.payload is None:
    sys.exit(0)

if qualification_paths is not None and not args.qualified_vuart:
    ap.error("native UART qualification requires --qualified-vuart before hardware access")

if not args.device:
    ap.error("--device or M1N1DEVICE is required for a hardware run")

if qualification_paths is None:
    os.environ.setdefault("M1N1DEVICE", args.device)
else:
    os.environ["M1N1DEVICE"] = args.device
# Shared lock covers direct recovery as well as native entry, and is inherited
# from run-assisted.sh when that wrapper already owns bootstrap admission.
from tools.ans_launch_lock import acquire as acquire_launch_lock
from tools.ans_qualification import reject_running_launcher
try:
    assisted_lock_fd = acquire_launch_lock(pathlib.Path(__file__).resolve().parent)
    reject_running_launcher()
except ValueError as error:
    ap.error(str(error))
from tools.assisted_session import AssistedSession
assisted_session = AssistedSession(assisted_lock_fd)
try:
    if qualification_paths is not None:
        import subprocess
        # subprocess.run reaps its exact chainload child before propagating failure.
        subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve().parent /
                        "m1n1_windows/proxyclient/tools/chainload.py"),
                        str(args.qualified_m1n1)], check=True, pass_fds=(assisted_lock_fd,))
    reader_device = args.qualified_vuart if qualification_paths is not None else args.managed_vuart
    if reader_device:
        import time
        reader_root = pathlib.Path(__file__).resolve().parent
        owned_uart_reader = assisted_session.start_reader(
            [sys.executable, "-u", str(reader_root / "extra/uart-reader.py"), reader_device, "2400"],
            reader_root / "guest-uart-reader.log", reader_root)
        time.sleep(2)
        if owned_uart_reader.poll() is not None:
            ap.error("owned UART reader exited before guest bootstrap")
except BaseException:
    assisted_session.close()
    raise

# RC30: an experiment may run a proxyclient from another m1n1 tree (M1N1_PROXYCLIENT); default is the submodule.
sys.path.insert(0, os.environ.get("M1N1_PROXYCLIENT") or str(pathlib.Path(__file__).parent / "m1n1_windows" / "proxyclient"))

from m1n1.proxy import *
from m1n1.proxyutils import *
from m1n1.utils import *
from m1n1.hv import HV, TraceMode
from tools.j313_launch_descriptor import pack_descriptor
from tools import j313_ramdisk
from tools.launch_contract import encode_record

iface = UartInterface()
p = M1N1Proxy(iface, debug=False)
bootstrap_port(iface, p)
cpufreq_result = p.cpufreq_init()
if cpufreq_result != 0:
    raise RuntimeError(f"CPU frequency preflight failed: cpufreq_init={cpufreq_result}")
print("CPU frequency preflight: clusters raised to their configured performance states")
# EXP1084: Windows has no Apple SoC DVFS driver, so the P-cluster stays at
# m1n1's default P-state 7 (1956 MHz). WOM1_PCPU_PSTATE requests a fixed
# non-turbo operating point (tools/j313_cpufreq.py); unset keeps the default.
if os.environ.get("WOM1_PCPU_PSTATE"):
    from tools.j313_cpufreq import PCPU_MHZ, requested_pstate, set_pcpu_pstate
    _requested = int(os.environ["WOM1_PCPU_PSTATE"])
    _before, _after = set_pcpu_pstate(p, _requested)
    print(f"CPU frequency: P-cluster P-state {requested_pstate(_before)} -> "
          f"{requested_pstate(_after)} ({PCPU_MHZ[_requested]} MHz), "
          f"raw 0x{_before:x} -> 0x{_after:x}")
u = ProxyUtils(p, heap_size=(64 if expanded_ram else 768) * 1024 * 1024)
# EXP2246: DRAM controller mode follows the P-cluster operating point like macOS
# (tools/j313_cpufreq.py apply_dramcfg): mcc dramcfg-data[0] while the P-cluster
# is at or above pmgr mcx-fast-pcpu-frequency.  Opt-in until hardware-validated.
if os.environ.get("WOM1_DRAMCFG_FOLLOW_PSTATE") == "1":
    from tools.j313_cpufreq import PCPU_MHZ, apply_dramcfg, dramcfg_fast_wanted
    _mcc = u.adt["/arm-io/mcc"]
    _pcpu_mhz = PCPU_MHZ[int(os.environ.get("WOM1_PCPU_PSTATE") or 7)]
    _threshold = getattr(u.adt["/arm-io/pmgr"], "mcx_fast_pcpu_frequency", None)
    _fast = dramcfg_fast_wanted(_pcpu_mhz, _threshold)
    _rows = apply_dramcfg(p, _mcc.get_reg(2)[0], int(_mcc._properties["dcs_num_channels"]),
                          list(_mcc.dramcfg_data), _fast)
    print(f"DRAMCFG: P-cluster {_pcpu_mhz} MHz, fast threshold {_threshold} MHz -> "
          f"{'FAST' if _fast else 'boot values'}; ch0 0x{_rows[0][1]:x}/0x{_rows[0][2]:x} -> "
          f"0x{_rows[0][3]:x}/0x{_rows[0][4]:x} on {len(_rows)} channels")
hv = HV(iface, p, u)

# Pin the guest's physical base so the load address stops moving between boots.
natural = u.heap_top
if args.no_pin:
    args.phys_base = natural
else:
    u.heap_top = args.phys_base

memory_base = args.phys_base
if expanded_ram:
    from m1n1.hv.guest_memory import expanded_ram_base
    memory_base = expanded_ram_base(u, 0x818000000)
    if p.heapblock_alloc(0) > u.heap.offset:
        raise RuntimeError("native heap has crossed into the proxy heap")
print(f"Guest RAM profile {args.guest_ram_profile}: base=0x{memory_base:x}; "
      f"proxy heap=0x{u.heap.offset:x}..0x{natural:x}")

# Mirror load_raw()'s arithmetic so we can predict the image base without loading.
tc_start, tc_size = u.adt["chosen"]["memory-map"].TrustCache
guest_base = args.phys_base + (16 << 20) + align(u.ba.devtree_size) + align(tc_size)

# Publish the same fixed-width descriptor used by autonomous Stage 1 before
# hv_init() mutates any state. The address arithmetic is deliberately copied
# from HV.load_raw(); the assertion below keeps both implementations locked.
payload_bytes = args.payload.read_bytes()
_, sepfw_size = u.adt["chosen"]["memory-map"].SEPFW
preoslog_size = 0
if hasattr(u.adt["chosen"]["memory-map"], "preoslog"):
    _, preoslog_size = u.adt["chosen"]["memory-map"].preoslog
bootargs_off = align(len(payload_bytes)) + align(sepfw_size) + preoslog_size
bootargs_base = guest_base + bootargs_off
mem_top = u.ba.phys_base + u.ba.mem_size
adt_blob = u.adt.build()
uart_base = u.adt["/arm-io/uart0"].get_reg(0)[0]
dart_base = u.adt["/arm-io/dart-usb1"].get_reg(0)[0]
# M1: decide about the boot medium before m1n1 sees the descriptor. A preloaded
# image is published as a ramdisk region (m1n1 keeps GPU DMA out of it); without
# one the window is ordinary guest RAM for Mu, Windows and the GPU alike.
ramdisk_blob = None
ramdisk_regions = ()
if args.ramdisk:
    ramdisk_blob = args.ramdisk.read_bytes()
    try:
        j313_ramdisk.check_fits(len(ramdisk_blob), args.ramdisk_base, args.ramdisk_max_size,
                                args.phys_base, mem_top)
    except ValueError as error:
        sys.exit(str(error))
    ramdisk_regions = (j313_ramdisk.descriptor_region(args.ramdisk_base, args.ramdisk_max_size),)
descriptor = pack_descriptor(
    boot=(memory_base, mem_top - memory_base, guest_base,
          (bootargs_base, 0, 0, 0)),
    regions=(
        (0, 0, memory_base, mem_top - memory_base),
        (1, 0, args.phys_base, 16 << 20),
        (2, 0, guest_base, align(len(payload_bytes))),
        (3, 0, args.phys_base + (16 << 20), align(u.ba.devtree_size)),
        (4, 0, bootargs_base, 0x4000),
        (5, 0, fb.base, fb.size),
        (6, 0, args.low_mem_backing, args.low_mem_size if args.low_mem else 0),
        # A bounded getter for the dynamically allocated DART page tables is
        # still pending. Zero size + UNOBSERVED is explicit, never guessed.
        (7, 1, args.phys_base, 0),
    ) + ramdisk_regions,
    devices=(layout.pci_ecam, 0, layout.xhci_base, dart_base, uart_base,
             fb.base, fb.width, fb.height, fb.stride, 0),
    adt_size=len(adt_blob),
    adt_digest=hashlib.sha256(adt_blob).digest(),
)
launch_contract_available = True
try:
    launch_descriptor_accepted = p.hv_launch_publish(descriptor)
except ProxyCommandError:
    if os.environ.get("WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT") != "1":
        raise
    # Frozen pre-public m1n1 images predate the launch-contract opcodes. This
    # escape hatch exists only for controlled binary A/B runs: normal public
    # launches stay fail-closed so a mismatched Stage 1 can never pass silently.
    launch_contract_available = False
    print("Compatibility: legacy launch-contract opcodes unavailable; "
          "checkpoint publication disabled for this A/B")
else:
    if not launch_descriptor_accepted:
        raise RuntimeError("m1n1 rejected the J313 launch descriptor")

if args.contract_output:
    args.contract_output.parent.mkdir(parents=True, exist_ok=True)
    args.contract_output.write_bytes(b"")

def capture_contract(checkpoint, sequence):
    if not launch_contract_available:
        print(f"LAUNCH_CONTRACT checkpoint={checkpoint} skipped for legacy A/B")
        return
    payload = p.hv_launch_capture(checkpoint, sequence)
    if payload is None:
        raise RuntimeError(f"launch contract checkpoint {checkpoint} failed")
    if args.contract_output:
        with args.contract_output.open("ab") as stream:
            stream.write(encode_record(payload))
            stream.flush()
            os.fsync(stream.fileno())
    print(f"LAUNCH_CONTRACT checkpoint={checkpoint} sequence={sequence} captured")

capture_contract(0, 1)

print("=" * 60)
print(f"m1n1 base (varies per boot) : 0x{u.base:x}")
print(f"natural heap_top           : 0x{natural:x}")
print(f"pinned phys_base           : 0x{args.phys_base:x}")
print(f"=> build the FD for        : 0x{guest_base:x}")
print("=" * 60)

#
# A watchdog consumes an entire physical CPU: HV.setup_adt() removes that CPU from the
# guest ADT and hv_wdt_start() keeps it in a host-side loop.  Therefore it is opt-in and
# must only be used with firmware whose MADT leaves the selected CPU disabled.  In
# particular, reserving CPU7 while an eight-core MADT advertises it makes Windows issue a
# PSCI CPU_ON for a core that can never enter the guest.
hv.wdt_cpu = args.wdt_cpu

snapshot_reboot_requested = False

def request_snapshot_reboot(signum, frame):
    global snapshot_reboot_requested
    snapshot_reboot_requested = True
    # Reuse m1n1's USER_INTERRUPT path so the C side publishes the same
    # pre-rendezvous lock-free records before the host chooses recovery policy.
    hv._handle_sigint(signum, frame)

signal.signal(signal.SIGTERM, request_snapshot_reboot)

def unattended_control(*_args, **_kwargs):
    # The C side prints lock-free per-vCPU records before attempting its SMP
    # rendezvous.  Do not issue nested proxy commands from this handler: a
    # partially wedged guest cannot service them reliably.  A diagnostic signal
    # is deliberately non-destructive; recovery/reboot is a separate explicit
    # operation so repeated snapshots can prove whether CPUs still advance.
    global snapshot_reboot_requested
    if snapshot_reboot_requested:
        snapshot_reboot_requested = False
        print("HOST CONTROL: diagnostic snapshot captured; rebooting Air")
        return EXC_RET.UNHANDLED
    if args.snapshot_kernel_read and hv.ctx is not None:
        # Read-only EL2 translate + memory read on the CPU servicing the proxy.
        from tools.ans_snapshot import parse as snapshot_parse, snapshot_kernel_read
        try:
            rva, length = snapshot_parse(args.snapshot_kernel_read)
            print(snapshot_kernel_read(lambda va: hv.p.hv_translate(va, False, False),
                                       hv.iface.readmem, hv.ctx.elr, rva, length))
        except Exception as error:  # diagnostics must never change recovery policy
            print(f"SNAPSHOT_KERNEL error: {error}")
    if args.snapshot_devnodes and hv.ctx is not None:
        from tools.ans_snapshot import parse_devnodes, snapshot_devnodes
        try:
            for line in snapshot_devnodes(lambda va: hv.p.hv_translate(va, False, False), hv.iface.readmem,
                                          hv.ctx.elr, parse_devnodes(args.snapshot_devnodes)):
                print(line)
        except Exception as error:
            print(f"SNAPSHOT_DEVNODE error: {error}")
    if args.snapshot_modules and hv.ctx is not None:
        from tools.ans_snapshot import parse_modules, snapshot_modules
        try:
            for line in snapshot_modules(lambda va: hv.p.hv_translate(va, False, False), hv.iface.readmem,
                                         hv.ctx.elr, parse_modules(args.snapshot_modules)):
                print(line)
        except Exception as error:
            print(f"SNAPSHOT_MODULE error: {error}")
    print("HOST CONTROL: diagnostic snapshot captured; continuing guest")
    return EXC_RET.HANDLED

hv.run_shell = unattended_control

hv.init()
capture_contract(1, 2)
# hv_init() installs the in-hypervisor xHCI MMIO diagnostic hook.  Keep the final
# pt_update() from replacing its 16 KiB stage-2 entry with the broad /arm-io hardware
# pass-through mapping (the same ordering rule as the PCI ECAM C hook).
hv.add_tracer(irange(0x502280000, 0x4000), "J313-XHCI-C-HOOK", TraceMode.RESERVED)
hv.tba.video.base = fb.base
hv.tba.video.width = fb.width
hv.tba.video.height = fb.height
hv.tba.video.stride = fb.stride
hv.tba.video.depth = 32
hv.tba.video.display = 1
hv.load_raw(payload_bytes, entryoffset=0,
            **({"memory_base": memory_base} if expanded_ram else {}))
assert args.no_pin or hv.guest_base == guest_base, \
    f"predicted 0x{guest_base:x} but loader chose 0x{hv.guest_base:x}"
print("Firmware address matches the prediction.")

mem_top = u.ba.phys_base + u.ba.mem_size
occupied_windows = {
    "firmware": (hv.adt_base, hv.tba.top_of_kernel_data),
    "ramdisk": (args.ramdisk_base, args.ramdisk_base + args.ramdisk_max_size),
}
if args.low_mem:
    occupied_windows["low-memory backing"] = (
        args.low_mem_backing,
        args.low_mem_backing + args.low_mem_size,
    )
fb.validate((hv.phys_base, mem_top), occupied_windows)
print("Framebuffer validation against RAM/firmware/ramdisk: OK")

# The header page must match PRELOADED_RAMDISK_HEADER in Mu's PreloadedRamdisk.h: an
# 8-byte magic and a 64-bit size, with the payload one page in so the image stays
# aligned. Mu's PEI reads it with the MMU off, so the page is cleaned to the point of
# coherency after it is written.
if ramdisk_blob is not None:
    print(f"Loading disk image: {len(ramdisk_blob)/(1<<20):.1f} MiB -> "
          f"0x{args.ramdisk_base + j313_ramdisk.PAYLOAD_OFFSET:x}...")
    u.compressed_writemem(args.ramdisk_base + j313_ramdisk.PAYLOAD_OFFSET, ramdisk_blob, True)
    iface.writemem(args.ramdisk_base, j313_ramdisk.header(len(ramdisk_blob)))
else:
    # M1: no boot medium. Clear a header an earlier ramdisk launch may have left without
    # a SoC reset, so Mu gives the window to Windows as m1n1 does to GPU backing pages.
    iface.writemem(args.ramdisk_base, j313_ramdisk.cleared_header())
    print(f"Ramdisk window 0x{args.ramdisk_base:x}: no image, header page cleared "
          f"(system memory)")
p.dc_cvac(args.ramdisk_base, j313_ramdisk.PAYLOAD_OFFSET)

if args.low_mem:
    if args.low_mem_backing + args.low_mem_size > mem_top:
        sys.exit(f"backing 0x{args.low_mem_backing:x}+0x{args.low_mem_size:x} is outside "
                 f"guest RAM (ending at 0x{mem_top:x})")
    print(f"Mapping low memory: 0x{args.low_mem_base:x} + 0x{args.low_mem_size:x} "
          f"-> 0x{args.low_mem_backing:x}")
    hv.map_hw(args.low_mem_base, args.low_mem_backing, args.low_mem_size)

# The ordinary proxy event loop is the only USB reader. EL2 opportunistically appends chunks
# to the USB IN ring and skips a tick under backpressure; it never interrupts or pauses Windows.
# This callback runs inside the only proxy/CDC reader. Publishing a complete
# 16 MiB frame includes fsync(), so doing it inline can stop serial draining
# long enough to lose framing under monitor-mode console load. Assembly stays
# ordered here; only complete-frame disk publication uses a bounded worker.
receiver = FrameReceiver(fb, asynchronous_publish=True) if profile.virtual_display else None
telemetry = TelemetryRecorder(p) if profile.telemetry else None
telemetry_inline = profile.telemetry and os.environ.get("HANG_TELEMETRY_INLINE") == "1"

def handle_framebuffer_event(data):
    assert receiver is not None
    receiver.accept(data)
    #
    # Do NOT issue proxy requests from here by default. A FRAMEBUFFER event is delivered from
    # inside UartInterface.reply() while an outer request is still awaiting its own reply, so
    # any transaction started here interleaves with it and desynchronises the stream - the boot
    # then dies with "Reply command mismatch: Expected 0x01aa55ff, got 0x02aa55ff" before the
    # guest ever starts. (Exception callbacks are different: by the time they run, m1n1 is back
    # in command mode, which is why the HV can safely talk to the proxy from those.)
    # Set HANG_TELEMETRY_INLINE=1 to restore the old in-handler polling for experiments.
    #
    if telemetry_inline:
        telemetry.maybe_poll()

def handle_telemetry_event(data):
    assert telemetry is not None
    telemetry.accept_event(data)

if profile.virtual_display:
    iface.set_event_handler(EVENT.FRAMEBUFFER, handle_framebuffer_event)
if profile.telemetry:
    iface.set_event_handler(EVENT.TELEMETRY, handle_telemetry_event)

def configure_display_consumers():
    # hv_fb_stream_config() validates the IPA through the live stage-2 tables.
    # HV.start() invokes this hook only after its final pt_update().
    p.memset32(fb.base, 0, fb.size)
    if profile.physical_display:
        prepare = getattr(p, "display_prepare_guest_surface", None)
        if prepare is None:
            raise RuntimeError("m1n1 does not implement physical guest display handoff")
        if prepare(fb.base, fb.size, fb.width, fb.height, fb.stride, 32) != 1:
            raise RuntimeError("m1n1 rejected physical guest display handoff")
        print("Internal DCP guest surface enabled")
    if profile.virtual_display:
        if p.hv_fb_stream_config(fb.base, fb.size, fb.width, fb.height, fb.stride) != 1:
            raise RuntimeError("m1n1 rejected the virtual framebuffer mapping")
        print(f"Asynchronous framebuffer stream enabled: {fb.size / (1 << 20):.2f} MiB/frame")
    capture_contract(2, 3)
    capture_contract(3, 4)

hv.pre_guest_start = configure_display_consumers

print("Starting guest...")
hv.start()
