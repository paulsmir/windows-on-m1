#!/usr/bin/env python3
"""Generate the ANS contract only in the selected checkout; no device access."""
import argparse
import hashlib
import json
from pathlib import Path


def number(x):
    return int(x, 0) if isinstance(x, str) else int(x)


def validate(c, root, for_launch=False):
    root = Path(root)
    if c['default_mode'] != 'VNVME_RECOVERY' or c['storage_modes'] != ['VNVME_RECOVERY', 'ANS_NATIVE']:
        raise ValueError('invalid storage ownership modes')
    if c['dma'] != dict(prp_page_size=4096, lba_size=4096, maximum_transfer_length=131072, max_data_pages=33, payload_bounce=False):
        raise ValueError('unsupported V1 DMA contract')
    if c['queues'] != dict(admin_queues=1, io_queues=1, depth=64, reserved_admin_tags=2):
        raise ValueError('unsupported T8103 queue contract')
    if c['acpi_hid'] != 'APPLANS0' or c['acpi_uid'] != 0:
        raise ValueError('invalid ACPI identity')
    b, size = number(c['broker']['guest_base']), c['broker']['size']
    irq = c['interrupt']['guest_gsiv']
    if b < 0 or b % 16384 or size != 16384 or b + size > 2**64 or not 32 <= irq < 1020:
        raise ValueError('invalid synthetic resource allocation')
    layout = json.loads((root/'config/j313-guest-layout.json').read_text())
    gpu = json.loads((root/'config/j313-agx-g2.json').read_text())
    inp = json.loads((root/'config/j313-apple-input.json').read_text())
    used_irqs = {layout['nvme_vintid'], inp['interrupt']['guest_vintid'], 857}
    used_irqs.update(x['guest'] for x in gpu['interrupt_routes'])
    if irq in used_irqs:
        raise ValueError('guest IRQ collision')
    ranges = [(number(layout['phys_base']), number(layout['ram_end'])-number(layout['phys_base'])),
              (number(layout['low_mem_ipa']), number(layout['low_mem_size'])),
              (number(layout['pci_ecam']), 0x100000), (number(layout['pci_bar_window']), 0x20000000),
              (number(layout['xhci_base']), 0x100000)]
    ranges += [(number(x['base']), number(x['size'])) for x in gpu['synthetic_mmio_regions'].values()]
    gpu_regions = json.loads((root/'config/j313-agx.json').read_text())['regions']
    ranges += [(number(x['base']), number(x['size'])) for x in gpu_regions.values()]
    ranges += [(number(inp[k]['base']), number(inp[k]['size'])) for k in ('spi', 'ap_gpio', 'nub_gpio')]
    ranges += [(number(x['base']), number(x['size'])) for x in c['physical_resources_expected_from_pinned_dt'].values()]
    if any(b < a+n and a < b+size for a,n in ranges):
        raise ValueError('guest MMIO collision')
    # This gate is deliberately closed until implementation/build/hardware admission
    # criteria have a separate reviewed implementation. JSON booleans cannot open it.
    if for_launch:
        raise ValueError('native qualification candidate is not yet admitted')


def generate(root, check=False):
    root = Path(root)
    config = root/'config/j313-ans.json'
    schema = root/'config/apple-ans-broker-abi.json'
    c = json.loads(config.read_text()); abi = json.loads(schema.read_text())
    validate(c, root)
    digest = hashlib.sha256(config.read_bytes()+schema.read_bytes()).hexdigest()
    h = ['/* Generated; edit config/j313-ans.json and apple-ans-broker-abi.json. */',
         f'/* Source SHA-256: {digest} */', '#ifndef APPLE_ANS_BROKER_ABI_H',
         '#define APPLE_ANS_BROKER_ABI_H', '#include <stdint.h>',
         '#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__',
         '#error ANS ABI requires little endian', '#endif']
    values = dict(MAGIC=c['broker']['magic'], ABI_MAJOR=c['broker']['abi_major'],
                  ABI_MINOR=c['broker']['abi_minor'], QUEUE_DEPTH=64, ADMIN_TAGS=2,
                  MAX_TRANSFER=131072, PRP_PAGE_SIZE=4096, MAX_DATA_PAGES=33,
                  BROKER_BASE=c['broker']['guest_base']+'ULL', BROKER_SIZE=c['broker']['size'],
                  GUEST_GSIV=c['interrupt']['guest_gsiv'], FEATURE_READ_ONLY=1,
                  DMA_NONE=0, DMA_FROM_DEVICE=1, DMA_TO_DEVICE=2)
    h += [f'#define ANS_{k} {v}' for k,v in values.items()]
    for name, entries in abi['enums'].items():
        h.append(f'enum {name} {{')
        h += [f'    {name.upper()}_{entry} = {i},' for i,entry in enumerate(entries)]
        h.append('};')
    for name, desc in abi['structs'].items():
        h.append(f'struct {name} {{')
        h += [f'    {typ} {field};' for typ,field in desc['fields']]
        h += ['};', f'typedef char {name}_size_check[(sizeof(struct {name}) == {desc["size"]}) ? 1 : -1];']
    h += ['#endif', '']
    header = '\n'.join(h)
    base = number(c['broker']['guest_base']); size = c['broker']['size']
    asl = f'''// Generated from config/j313-ans.json; synthetic allocation, not physical ANS MMIO.
// Include in a profile that defines ANSN only after native ownership is armed.
Device (ANS0) {{
    Name (_HID, "APPLANS0")
    Name (_UID, Zero)
    Name (_CCA, One)
    Name (_CRS, ResourceTemplate () {{
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable,
                     ReadWrite, 0, 0x{base:016X}, 0x{base+size-1:016X}, 0, 0x{size:X})
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {{ {c['interrupt']['guest_gsiv']} }}
    }})
    Method (_STA, 0) {{ If (ANSN) {{ Return (0x0F) }} Return (Zero) }}
}}
'''
    outputs = {'drivers/storage/appleans/include/apple_ans_broker_abi.h': header,
               'm1n1_windows/src/hv_ans_broker_abi.h': header,
               'mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAns.asl.inc': asl}
    stale = []
    for rel, data in outputs.items():
        path = root/rel
        if not path.exists() or path.read_text() != data:
            stale.append(rel)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(data)
    return stale


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    changed = generate(Path(__file__).resolve().parents[1], a.check)
    if a.check and changed:
        raise SystemExit('stale generated files: '+', '.join(changed))
