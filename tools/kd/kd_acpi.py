#!/usr/bin/env python3
"""
Read and validate the live ACPI chain through Windows KD physical-memory requests:

    RSDP -> XSDT -> table headers -> MCFG -> ECAM allocations

Usage:
    python3 tools/kd/kd_acpi.py <RSDP_PHYS_ADDR>

Example:
    python3 tools/kd/kd_acpi.py 0x9dd9a6000

The script deliberately DOES NOT read the ECAM window. A direct KD physical read of ECAM
would itself hit the m1n1 stage-2 hook and contaminate the "first ECAM access" diagnostic.
"""

import serial
import struct
import sys

from kd_proclist import KD, BAUD, require_vuart


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def sig4(buf):
    return buf[:4].decode("ascii", "replace")


def checksum_ok(buf):
    return (sum(buf) & 0xFF) == 0


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <RSDP physical address>", file=sys.stderr)
        raise SystemExit(2)

    rsdp_pa = int(sys.argv[1], 0)

    ser = serial.Serial(require_vuart(), BAUD, timeout=0.06)
    kd = KD(ser)

    try:
        print("[*] break in...")
        kd.break_in()
        kd.get_version()

        def read_phys(addr, length, what):
            ret, data = kd.read_physical(addr, length)
            print(
                f"[*] {what}: PA=0x{addr:016x} len=0x{length:x} "
                f"status=0x{ret:08x} got=0x{len(data):x}"
            )
            if ret & 0x80000000:
                raise RuntimeError(
                    f"{what}: read_physical failed, NTSTATUS=0x{ret:08x}"
                )
            if len(data) != length:
                raise RuntimeError(
                    f"{what}: short physical read: wanted {length}, got {len(data)}"
                )
            return data

        # ACPI 2.0+ RSDP is 36 bytes. The first 20 bytes are common with ACPI 1.0.
        rsdp = read_phys(rsdp_pa, 36, "RSDP")

        if rsdp[:8] != b"RSD PTR ":
            print(f"[!] bad RSDP signature: {rsdp[:8]!r}")
            print(f"    raw: {rsdp.hex()}")
            raise SystemExit(1)

        revision = rsdp[15]
        rsdt_pa = u32(rsdp, 16)

        print("[+] RSDP signature valid")
        print(f"    OEM ID             = {rsdp[9:15].decode('ascii', 'replace')!r}")
        print(f"    revision           = {revision}")
        print(f"    RSDT PA            = 0x{rsdt_pa:016x}")
        print(f"    ACPI 1.0 checksum  = {'OK' if checksum_ok(rsdp[:20]) else 'BAD'}")

        if revision < 2:
            raise RuntimeError("RSDP is ACPI 1.0; no XSDT address is present")

        rsdp_len = u32(rsdp, 20)
        xsdt_pa = u64(rsdp, 24)

        if rsdp_len < 36 or rsdp_len > 4096:
            raise RuntimeError(f"implausible RSDP length: 0x{rsdp_len:x}")

        if rsdp_len != 36:
            rsdp = read_phys(rsdp_pa, rsdp_len, "full RSDP")

        print(f"    RSDP length        = 0x{rsdp_len:x}")
        print(f"    XSDT PA            = 0x{xsdt_pa:016x}")
        print(f"    extended checksum  = {'OK' if checksum_ok(rsdp) else 'BAD'}")

        # Standard ACPI description header is 36 bytes.
        xsdt_hdr = read_phys(xsdt_pa, 36, "XSDT header")
        if xsdt_hdr[:4] != b"XSDT":
            raise RuntimeError(
                f"XSDT address does not contain XSDT: signature={xsdt_hdr[:4]!r}"
            )

        xsdt_len = u32(xsdt_hdr, 4)
        if xsdt_len < 36 or xsdt_len > 0x10000:
            raise RuntimeError(f"implausible XSDT length: 0x{xsdt_len:x}")
        if (xsdt_len - 36) % 8:
            raise RuntimeError(
                f"XSDT entry area is not qword-aligned: length=0x{xsdt_len:x}"
            )

        xsdt = read_phys(xsdt_pa, xsdt_len, "full XSDT")
        entry_count = (xsdt_len - 36) // 8

        print("[+] XSDT")
        print(f"    length             = 0x{xsdt_len:x}")
        print(f"    revision           = {xsdt[8]}")
        print(f"    checksum           = {'OK' if checksum_ok(xsdt) else 'BAD'}")
        print(f"    entries            = {entry_count}")

        mcfg_tables = []

        for index in range(entry_count):
            table_pa = u64(xsdt, 36 + index * 8)
            hdr = read_phys(table_pa, 36, f"table[{index}] header")
            signature = sig4(hdr)
            length = u32(hdr, 4)
            revision = hdr[8]
            checksum = hdr[9]

            print(
                f"    [{index:02d}] {signature} PA=0x{table_pa:016x} "
                f"len=0x{length:x} rev={revision} header_checksum=0x{checksum:02x}"
            )

            if signature == "MCFG":
                mcfg_tables.append((table_pa, length))

        if not mcfg_tables:
            print("[!] MCFG is NOT referenced by the live XSDT")
            print("    This is before pci.sys/GSIV/ECAM: fix ACPI build or installation.")
            return

        if len(mcfg_tables) > 1:
            print(f"[!] warning: XSDT contains {len(mcfg_tables)} MCFG tables")

        for ordinal, (mcfg_pa, mcfg_len) in enumerate(mcfg_tables):
            if mcfg_len < 44 or mcfg_len > 0x10000:
                raise RuntimeError(f"implausible MCFG length: 0x{mcfg_len:x}")

            mcfg = read_phys(mcfg_pa, mcfg_len, f"full MCFG #{ordinal}")
            print(f"[+] MCFG #{ordinal}")
            print(f"    PA                 = 0x{mcfg_pa:016x}")
            print(f"    length             = 0x{mcfg_len:x}")
            print(f"    checksum           = {'OK' if checksum_ok(mcfg) else 'BAD'}")

            allocation_bytes = mcfg_len - 44
            if allocation_bytes % 16:
                print(
                    f"[!] malformed MCFG allocation area: "
                    f"0x{allocation_bytes:x} bytes"
                )
                continue

            count = allocation_bytes // 16
            print(f"    allocations        = {count}")

            for i in range(count):
                off = 44 + i * 16
                base = u64(mcfg, off)
                segment = u16(mcfg, off + 8)
                start_bus = mcfg[off + 10]
                end_bus = mcfg[off + 11]
                reserved = u32(mcfg, off + 12)

                print(
                    f"      [{i}] base=0x{base:016x} segment={segment} "
                    f"bus={start_bus:02x}..{end_bus:02x} "
                    f"reserved=0x{reserved:08x}"
                )

                if (
                    base == 0x690000000
                    and segment == 0
                    and start_bus == 0
                    and end_bus == 0
                ):
                    print("          EXPECTED tuple: yes")
                else:
                    print("          EXPECTED tuple: NO")

    finally:
        print("[*] go")
        try:
            kd.continue_execution()
        except Exception as exc:
            print(f"[!] continue failed: {exc!r}")
        ser.close()


if __name__ == "__main__":
    main()
