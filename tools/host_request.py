"""Host requests to a running guest, issued from the launcher's interrupt path.

The m1n1 proxy is serviced only while the guest is stopped in the hypervisor,
so a request is delivered as SIGUSR1: the launcher interrupts the guest, runs
every line of the request file through the proxy, prints one HOST_REQUEST
line per request and resumes the guest.  The file is consumed (renamed to
<file>.done) so a repeated signal never re-arms an old request.

Request lines (``#`` starts a comment):

    refuse N   RQ-2 0x110b: the next N owner swaps fail at the A408 stage
    drop N     RQ-2 0x110c: the next N exact latches are treated as lost
    status     RQ-2 0x110d: print the scanout recovery counters
    mse ECAM BUS DEV FN off|on
               BAR-fault reproduction (EXP2613 class): clear or set Memory Space
               Enable (Command bit 1) of one function through the physical ECAM, so
               the guest driver's next forwarded BAR access completes with an error.
               Prints the Command value before and after.  Nothing else is written.
    mmucfg     CACHE-1, read-only: the twelve 32-bit words of the GPU MMU configuration
               block at SGX+0xd08000 (0x204d08000..0x204d0802c; the EXP2235 receipt read
               the same words from EL2).  Nothing is written.  Issue it only while the GPU
               is powered (during a GPU run): the block is in the GPU power domain.
"""

import pathlib
import struct

P_DISPLAY_INJECT_REFUSE_SWAPS = 0x110B
P_DISPLAY_INJECT_DROP_LATCHES = 0x110C
P_DISPLAY_SCANOUT_RECOVERY_STATUS = 0x110D
SGX_MMUCFG_BASE = 0x204D08000
SGX_MMUCFG_WORDS = 12
STATUS_SIZE = 40
STATUS_FIELDS = ("latch_deadline_expiries", "latch_poll_errors", "late_latches_ignored",
                 "injected_refusals", "dropped_latches", "pending_refuse_swaps",
                 "pending_drop_latches", "abandoned_swap_id")
MAX_COUNT = 64
PCI_COMMAND = 0x04
PCI_COMMAND_MSE = 0x2


def parse(text):
    """Return [(verb, count)]; raise ValueError on any malformed line."""
    requests = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        words = line.split()
        verb = words[0].lower()
        if verb == "status" and len(words) == 1:
            requests.append(("status", 0))
            continue
        if verb == "mse" and len(words) == 6 and words[5].lower() in ("off", "on"):
            try:
                ecam, bus, dev, fn = (int(w, 0) for w in words[1:5])
            except ValueError:
                raise ValueError(f"line {number}: cannot parse {raw!r}") from None
            if ecam & 0xfffff or not (1 <= bus <= 255 and 0 <= dev <= 31 and 0 <= fn <= 7):
                raise ValueError(f"line {number}: bad ECAM/BDF in {raw!r}")
            requests.append(("mse", (ecam, bus, dev, fn, words[5].lower() == "on")))
            continue
        if verb == "mmucfg" and len(words) == 1:
            requests.append(("mmucfg", 0))
            continue
        if verb in ("refuse", "drop") and len(words) == 2 and words[1].isdigit():
            count = int(words[1])
            if count > MAX_COUNT:
                raise ValueError(f"line {number}: count {count} above {MAX_COUNT}")
            requests.append((verb, count))
            continue
        raise ValueError(f"line {number}: cannot parse {raw!r}")
    return requests


def decode_status(data):
    return dict(zip(STATUS_FIELDS, struct.unpack("<QQIIIIII", data)))


def execute(proxy, requests):
    """Run parsed requests through `proxy`; return the HOST_REQUEST lines."""
    lines = []
    for verb, count in requests:
        if verb == "status":
            address = proxy.heap.malloc(STATUS_SIZE)
            try:
                ok = proxy.request(P_DISPLAY_SCANOUT_RECOVERY_STATUS, address, STATUS_SIZE)
                status = decode_status(proxy.iface.readmem(address, STATUS_SIZE)) if ok else None
            finally:
                proxy.heap.free(address)
            body = " ".join(f"{k}={v}" for k, v in status.items()) if status else "unavailable"
            lines.append(f"HOST_REQUEST status {body}")
            continue
        if verb == "mse":
            ecam, bus, dev, fn, on = count
            reg = ecam + (bus << 20) + (dev << 15) + (fn << 12) + PCI_COMMAND
            before = proxy.read16(reg)
            after = (before | PCI_COMMAND_MSE) if on else (before & ~PCI_COMMAND_MSE)
            proxy.write16(reg, after)
            lines.append(f"HOST_REQUEST mse {bus:02x}:{dev:02x}.{fn} {'on' if on else 'off'} "
                         f"command 0x{before:04x} -> 0x{proxy.read16(reg):04x}")
            continue
        if verb == "mmucfg":
            values = [proxy.read32(SGX_MMUCFG_BASE + 4 * i) for i in range(SGX_MMUCFG_WORDS)]
            lines.append("HOST_REQUEST mmucfg " + " ".join(f"{v:08x}" for v in values))
            continue
        op = P_DISPLAY_INJECT_REFUSE_SWAPS if verb == "refuse" else P_DISPLAY_INJECT_DROP_LATCHES
        previous = proxy.request(op, count, signed=True)
        lines.append(f"HOST_REQUEST {verb} count={count} previous_pending={previous}")
    return lines


def consume(path, proxy):
    """Read, execute and retire the request file; return the printed lines."""
    path = pathlib.Path(path)
    if not path.exists():
        return ["HOST_REQUEST none (no request file)"]
    text = path.read_text()
    path.rename(path.with_name(path.name + ".done"))
    try:
        requests = parse(text)
    except ValueError as error:
        return [f"HOST_REQUEST rejected {error}"]
    return execute(proxy, requests)
