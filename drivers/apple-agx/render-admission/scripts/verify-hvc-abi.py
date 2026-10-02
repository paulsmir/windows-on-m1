#!/usr/bin/env python3
"""Execute a pinned-MSVC HVC probe DLL against the private X0/status ABI.

Requires pefile==2024.8.26 and unicorn==2.1.4 in an isolated test environment.
Build tests/hvc_abi_probe.c both with and without USE_EXPLICIT_HVC_ABI. The old
intrinsic DLL must fail; the explicit-assembly DLL must pass. No real HVC runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import pefile
import unicorn
from unicorn import arm64_const as regs


def run_case(pe, export, immediate, status):
    base = pe.OPTIONAL_HEADER.ImageBase
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
    uc.mem_map(base, size)
    uc.mem_write(base, pe.get_memory_mapped_image())
    stack, stop = 0x10000000, 0x20000000
    uc.mem_map(stack, 0x10000)
    uc.mem_map(stop, 0x1000)
    uc.reg_write(regs.UC_ARM64_REG_SP, stack + 0xfff0)
    uc.reg_write(regs.UC_ARM64_REG_LR, stop)
    payload = 0x100003456
    uc.reg_write(regs.UC_ARM64_REG_X0, payload)
    uc.reg_write(regs.UC_ARM64_REG_X8, 0x8877665544332211)
    saved = {}
    for index in range(19, 30):
        register = getattr(regs, f'UC_ARM64_REG_X{index}')
        saved[register] = 0xabc00000 + index
        uc.reg_write(register, saved[register])
    boundary, calls = [], []

    def code_hook(machine, address, length, _):
        instruction = struct.unpack('<I', machine.mem_read(address, 4))[0]
        if instruction & 0xffe0001f == 0xd4000002:
            boundary.append((address, (instruction >> 5) & 0xffff,
                             machine.reg_read(regs.UC_ARM64_REG_X0)))
            machine.emu_stop()

    uc.hook_add(unicorn.UC_HOOK_CODE, code_hook)
    pc = base + export.address
    for _ in range(3):
        uc.emu_start(pc, stop, count=100)
        if not boundary:
            break
        address, actual_immediate, actual_payload = boundary.pop()
        calls.append((actual_immediate, actual_payload))
        # Model the documented private handler and architectural HVC return:
        # only X0 is changed, ELR resumes at the instruction after HVC.
        uc.reg_write(regs.UC_ARM64_REG_X0, status)
        pc = address + 4
    name = export.name.decode()
    expected = int(status == 1) if name == 'CompareArm' else status
    actual = uc.reg_read(regs.UC_ARM64_REG_X0)
    preserved = all(uc.reg_read(k) == v for k, v in saved.items())
    returned = uc.reg_read(regs.UC_ARM64_REG_PC) == stop
    passed = (calls == [(immediate, payload)] and actual == expected and
              preserved and returned and
              uc.reg_read(regs.UC_ARM64_REG_SP) == stack + 0xfff0)
    return dict(Function=name, Status=status, Expected=expected, Actual=actual,
                Calls=calls, Preserved=preserved, Returned=returned, PASS=passed)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dll', type=Path)
    args = parser.parse_args()
    pe = pefile.PE(str(args.dll))
    if pe.FILE_HEADER.Machine != 0xaa64:
        raise SystemExit('Expected actual ARM64 PE code')
    exports = {symbol.name.decode(): symbol for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols}
    rows = [run_case(pe, exports[name], imm, status)
            for name, imm in [('CallArm', 0x4d32), ('CallTranslate', 0x4d31),
                              ('CompareArm', 0x4d32)] for status in (1, 2)]
    print(json.dumps(dict(DLL=str(args.dll), SHA256=hashlib.sha256(args.dll.read_bytes()).hexdigest(),
                          Cases=rows, PASS=all(row['PASS'] for row in rows)), indent=2))
    return 0 if all(row['PASS'] for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
