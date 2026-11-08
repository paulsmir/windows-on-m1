#!/usr/bin/env python3
"""Read-only exact launcher selection. Prints one PID; never signals a process.

Darwin argv uses KERN_PROCARGS2, not shell parsing of lossy ps command text.
Original ABI parser, based on XNU sysctl_procargsx and SDK sys/sysctl.h;
no external implementation code is copied. Other platforms fail closed.
"""
import argparse
import ctypes
import errno
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


class SelectionError(RuntimeError):
    pass


def decode_procargs(blob):
    """Decode argc, executable path, padding, then exactly argc NUL strings."""
    if len(blob) < 5:
        raise SelectionError('truncated procargs header')
    argc = struct.unpack_from('@i', blob)[0]
    if argc < 1 or argc > len(blob):
        raise SelectionError('invalid procargs argc')
    end = blob.find(b'\0', 4)
    if end <= 4:
        raise SelectionError('missing executable path')
    executable = os.fsdecode(blob[4:end])
    pos = end + 1
    while pos < len(blob) and blob[pos] == 0:
        pos += 1
    argv = []
    for _ in range(argc):
        end = blob.find(b'\0', pos)
        if end < 0:
            raise SelectionError('truncated procargs argv')
        argv.append(os.fsdecode(blob[pos:end]))
        pos = end + 1
    return executable, argv


def read_proc_cmdline(pid):
    """Linux: argv from /proc/<pid>/cmdline (NUL-terminated strings), the executable from /proc/<pid>/exe."""
    base = Path('/proc') / str(pid)
    try:
        raw = (base / 'cmdline').read_bytes()
        executable = os.readlink(base / 'exe')
    except FileNotFoundError as exc:
        raise OSError(errno.ESRCH, 'process vanished') from exc
    if not raw:
        raise OSError(errno.ESRCH, 'process vanished or is a zombie')
    if not raw.endswith(b'\0'):
        raise SelectionError('truncated /proc cmdline')
    return executable, [os.fsdecode(arg) for arg in raw[:-1].split(b'\0')]


def read_procargs(pid):
    if sys.platform.startswith('linux'):
        return read_proc_cmdline(pid)
    if sys.platform != 'darwin':
        raise SelectionError('only Darwin KERN_PROCARGS2 and Linux /proc are supported')
    libc = ctypes.CDLL(None, use_errno=True)
    call = libc.sysctl
    call.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint,
                     ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                     ctypes.c_void_p, ctypes.c_size_t]
    call.restype = ctypes.c_int
    def query(mib, buf, size):
        names = (ctypes.c_int * len(mib))(*mib)
        if call(names, len(mib), buf, ctypes.byref(size), None, 0):
            number = ctypes.get_errno()
            raise OSError(number, os.strerror(number))
    # SDK: CTL_KERN=1, KERN_ARGMAX=8, KERN_PROCARGS2=49.
    limit = ctypes.c_int()
    size = ctypes.c_size_t(ctypes.sizeof(limit))
    query([1, 8], ctypes.byref(limit), size)
    if not 4096 <= limit.value <= 16 * 1024 * 1024:
        raise SelectionError('unexpected KERN_ARGMAX')
    buf = ctypes.create_string_buffer(limit.value)
    size = ctypes.c_size_t(len(buf))
    query([1, 49, pid], buf, size)
    return decode_procargs(buf.raw[:size.value])


def same_file(value, expected):
    path = Path(value)
    if not path.is_absolute():
        raise SelectionError('relative launcher/contract path cannot prove identity')
    try:
        return path.samefile(expected)
    except OSError as exc:
        raise SelectionError('cannot establish launcher/contract file identity') from exc


def python_name(value):
    return re.fullmatch(r'(?:Python|python(?:\d+(?:\.\d+)*)?)', Path(value).name) is not None


def is_owner(executable, argv, launcher, contract):
    if not argv or not python_name(executable) or not python_name(argv[0]):
        return False
    index = 1
    # Deliberately restricted direct-script launch grammar. -c/-m are never owners.
    if any(arg in ('-c', '-m') for arg in argv[1:]):
        return False
    while index < len(argv) and argv[index] in ('-u', '-B', '-E', '-I', '-s', '-S', '-O', '-OO'):
        index += 1
    if index < len(argv) and argv[index] == '--':
        index += 1
    if index == len(argv):
        return False
    script = argv[index]
    if Path(script).name != 'run_uefi.py':
        if any(Path(arg).name == 'run_uefi.py' for arg in argv[index:]):
            raise SelectionError('ambiguous Python launcher grammar')
        return False
    if not same_file(script, launcher):
        return False
    values = []
    args = argv[index + 1:]
    # Validate the current run_uefi.py CLI grammar without importing/executing it.
    # Unknown future options fail closed until this read-only selector is reviewed.
    class StrictParser(argparse.ArgumentParser):
        def error(self, message):
            raise SelectionError('malformed launcher command: ' + message)
    grammar = StrictParser(add_help=False, allow_abbrev=False)
    grammar.add_argument('payload', nargs='?')
    for option in ('layout', 'device', 'ramdisk', 'contract-output'):
        grammar.add_argument('--' + option)
    for option in ('phys-base', 'ramdisk-base', 'ramdisk-max-size', 'low-mem-base',
                   'low-mem-size', 'low-mem-backing', 'fb-base', 'guest-ram-end'):
        grammar.add_argument('--' + option, type=lambda value: int(value, 0))
    for option in ('fb-width', 'fb-height'):
        grammar.add_argument('--' + option, type=int)
    grammar.add_argument('--wdt-cpu', type=int, choices=range(8))
    grammar.add_argument('--display-mode', choices=('none', 'physical', 'virtual', 'both'))
    grammar.add_argument('--debug-mode', choices=('off', 'uart', 'full', 'monitor'))
    # rc30: run_uefi --guest-ram-profile (EXP1961)
    grammar.add_argument('--guest-ram-profile', choices=('legacy', 'expanded'))
    for option in ('dry-run', 'low-mem', 'no-pin'):
        grammar.add_argument('--' + option, action='store_true')
    # 2026-11-08: production ANS-DIRECT launches (run_uefi ef1707777): explicit native-SSD ownership.
    grammar.add_argument('--storage-mode', choices=('VNVME_RECOVERY', 'ANS_NATIVE'))
    grammar.add_argument('--ans-direct', action='store_true')
    grammar.add_argument('--ans-production-fd-sha256')
    parsed = grammar.parse_args(args)
    if parsed.dry_run or '--' in args:
        raise SelectionError('dry-run/end-of-options cannot own recovery launcher')
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == '--contract-output':
            i += 1
            if i == len(args) or args[i].startswith('-'):
                raise SelectionError('missing contract-output value')
            values.append(args[i])
        elif arg.startswith('--contract-output='):
            values.append(arg.split('=', 1)[1])
        elif arg.startswith('--contract'):
            raise SelectionError('ambiguous contract option')
        i += 1
    if len(values) != 1 or not values[0]:
        raise SelectionError('expected exactly one contract-output option')
    return same_file(values[0], contract)


def select_owner(pids, reader, launcher, contract):
    owners = []
    snapshots = {}
    for pid in pids:
        if pid <= 0 or pid in snapshots:
            raise SelectionError('invalid/duplicate process identity')
        try:
            snapshot = reader(pid)
        except OSError as exc:
            # ESRCH is a proven vanished process; Darwin EINVAL is ambiguous.
            if exc.errno == errno.ESRCH:
                continue
            raise SelectionError('cannot read candidate process argv') from exc
        snapshots[pid] = snapshot
        if is_owner(*snapshot, launcher, contract):
            owners.append(pid)
    if len(owners) != 1:
        raise SelectionError('expected exactly one owner; found %d' % len(owners))
    pid = owners[0]
    try:
        current = reader(pid)
    except OSError as exc:
        raise SelectionError('owner disappeared during verification') from exc
    if current != snapshots[pid] or not is_owner(*current, launcher, contract):
        raise SelectionError('owner argv changed during verification')
    return pid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('experiment_dir', type=Path)
    parser.add_argument('--worktree', type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    try:
        launcher = (args.worktree / 'run_uefi.py').resolve(strict=True)
        contract = (args.experiment_dir / 'contract.bin').resolve(strict=True)
        if not launcher.is_file() or not contract.is_file():
            raise SelectionError('launcher and contract must be existing regular files')
        # ps discovers candidates only. Its serialized command is NEVER parsed as argv.
        output = subprocess.run(['/bin/ps', '-axww' if sys.platform == 'darwin' else 'axww', '-o', 'pid=,command='],
                                check=True, capture_output=True, text=True, timeout=10).stdout
        pids = []
        for row in output.splitlines():
            if 'run_uefi.py' in row:
                parts = row.lstrip().split(None, 1)
                if len(parts) != 2 or not parts[0].isdigit():
                    raise SelectionError('malformed process snapshot')
                pids.append(int(parts[0]))
        print(select_owner(pids, read_procargs, launcher, contract))
    except (SelectionError, OSError, subprocess.SubprocessError) as exc:
        print('EXACT_OWNER_REFUSED: ' + str(exc), file=sys.stderr)
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
