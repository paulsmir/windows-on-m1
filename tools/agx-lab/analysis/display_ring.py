# Decode Wom1DisplayRing1131.bin (EXP1131). argv: bin [settings-done.txt (utf-8)]
# Header: Version Bytes Next Flips (4 x u32), LastFlipQpc (i64), LastFlip Kind Flags Detail Reserved (4 x u32);
# entries: Qpc Address Allocation (3 x u64), Kind Flags Detail Gap (4 x u32).
import struct, sys, re
b = open(sys.argv[1], 'rb').read()
ver, size, nxt, flips = struct.unpack_from('<4I', b, 0)
hdr = 40 if size == 40 + 256 * 40 else 48  # EXP1131 v1 header 40 B, v1b (per-kind last flip) 48 B
lastq, lk = 0, 0
n = 256; ents = []
for i in range(min(nxt, n)):
    slot = (nxt - min(nxt, n) + i) % n
    q, a, al, k, f, d, g = struct.unpack_from('<3Q4I', b, hdr + 40 * slot)
    ents.append((q, a, al, k, f, d, g))
print(f'version {ver} bytes {size} next {nxt} flips {flips} lastflip qpc {lastq} kind {lk}')
freq = 24000000; marks = []
if len(sys.argv) > 2:
    for line in open(sys.argv[2], encoding='utf-8', errors='replace'):
        m = re.match(r'qpcfreq (\d+)', line)
        if m: freq = int(m.group(1))
        m = re.match(r'(open|close) (\S+) qpc (\d+)', line)
        if m: marks.append((int(m.group(3)), m.group(1) + ' ' + m.group(2)))
names = {1: 'VISIBLE', 2: 'COMMITVIDPN', 3: 'SETADDR', 4: 'MPO3'}
t0 = ents[0][0] if ents else 0
rows = [(q, f'{names.get(k, k):11} flags={f:#x} detail={d:#x} gap={g/1000:.1f}ms addr={a:#x} alloc={al:#x}') for q, a, al, k, f, d, g in ents]
rows += [(q, '---- ' + s) for q, s in marks]
for q, s in sorted(rows):
    print(f'{(q - t0) / freq:10.3f}s  {s}')
