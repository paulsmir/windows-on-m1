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

PHYS_BASE must sit above m1n1 + its 768 MB heap and below the top of RAM.
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
ap.add_argument("--layout", type=pathlib.Path, default=DEFAULT_LAYOUT_PATH,
                help="canonical J313 guest-layout JSON")
ap.add_argument("--device", default=os.environ.get("M1N1DEVICE"))
ap.add_argument("--display-mode", choices=("none", "physical", "virtual", "both"),
                default="virtual", help="guest framebuffer consumers")
ap.add_argument("--debug-mode", choices=("off", "uart", "full", "monitor"), default="uart",
                help="diagnostic work performed beside the guest")
ap.add_argument("--phys-base", type=lambda x: int(x, 0))
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
ap.add_argument("--contract-output", type=pathlib.Path,
                help="write four CRC-framed launch-contract checkpoints")
args = ap.parse_args()
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

if not args.device:
    ap.error("--device or M1N1DEVICE is required for a hardware run")

os.environ.setdefault("M1N1DEVICE", args.device)
sys.path.insert(0, str(pathlib.Path(__file__).parent / "m1n1_windows" / "proxyclient"))

from m1n1.proxy import *
from m1n1.proxyutils import *
from m1n1.utils import *
from m1n1.hv import HV, TraceMode
from tools.j313_launch_descriptor import pack_descriptor
from tools.launch_contract import encode_record

iface = UartInterface()
p = M1N1Proxy(iface, debug=False)
bootstrap_port(iface, p)
cpufreq_result = p.cpufreq_init()
if cpufreq_result != 0:
    raise RuntimeError(f"CPU frequency preflight failed: cpufreq_init={cpufreq_result}")
print("CPU frequency preflight: clusters raised to their configured performance states")
u = ProxyUtils(p, heap_size=768 * 1024 * 1024)
hv = HV(iface, p, u)

# Pin the guest's physical base so the load address stops moving between boots.
natural = u.heap_top
if args.no_pin:
    args.phys_base = natural
else:
    u.heap_top = args.phys_base

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
descriptor = pack_descriptor(
    boot=(args.phys_base, mem_top - args.phys_base, guest_base,
          (bootargs_base, 0, 0, 0)),
    regions=(
        (0, 0, args.phys_base, mem_top - args.phys_base),
        (1, 0, args.phys_base, 16 << 20),
        (2, 0, guest_base, align(len(payload_bytes))),
        (3, 0, args.phys_base + (16 << 20), align(u.ba.devtree_size)),
        (4, 0, bootargs_base, 0x4000),
        (5, 0, fb.base, fb.size),
        (6, 0, args.low_mem_backing, args.low_mem_size if args.low_mem else 0),
        # A bounded getter for the dynamically allocated DART page tables is
        # still pending. Zero size + UNOBSERVED is explicit, never guessed.
        (7, 1, args.phys_base, 0),
    ),
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
hv.load_raw(payload_bytes, entryoffset=0)
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

if args.ramdisk:
    # Layout must match PRELOADED_RAMDISK_HEADER in BootRamdiskHelperDxe.h: an 8-byte
    # magic and a 64-bit size, with the payload one page in so the image stays aligned.
    PAYLOAD_OFF = 0x1000
    blob = args.ramdisk.read_bytes()
    room = args.ramdisk_max_size - PAYLOAD_OFF
    if len(blob) > room:
        sys.exit(f"image size {len(blob)} does not fit in the {room}-byte window "
                 f"(increase PcdPreloadedRamdiskMaxSize and --ramdisk-max-size)")

    if not (args.phys_base <= args.ramdisk_base and
            args.ramdisk_base + args.ramdisk_max_size <= mem_top):
        sys.exit(f"window 0x{args.ramdisk_base:x}+0x{args.ramdisk_max_size:x} is outside "
                 f"guest RAM 0x{args.phys_base:x}..0x{mem_top:x}")

    header = b"ASIRAMDK" + len(blob).to_bytes(8, "little")
    print(f"Loading disk image: {len(blob)/(1<<20):.1f} MiB -> "
          f"0x{args.ramdisk_base + PAYLOAD_OFF:x}...")
    u.compressed_writemem(args.ramdisk_base + PAYLOAD_OFF, blob, True)
    iface.writemem(args.ramdisk_base, header.ljust(PAYLOAD_OFF, b"\0"))

if args.low_mem:
    if args.low_mem_backing + args.low_mem_size > mem_top:
        sys.exit(f"backing 0x{args.low_mem_backing:x}+0x{args.low_mem_size:x} is outside "
                 f"guest RAM (ending at 0x{mem_top:x})")
    print(f"Mapping low memory: 0x{args.low_mem_base:x} + 0x{args.low_mem_size:x} "
          f"-> 0x{args.low_mem_backing:x}")
    hv.map_hw(args.low_mem_base, args.low_mem_backing, args.low_mem_size)

# The ordinary proxy event loop is the only USB reader. EL2 opportunistically appends chunks
# to the USB IN ring and skips a tick under backpressure; it never interrupts or pauses Windows.
receiver = FrameReceiver(fb) if profile.virtual_display else None
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
