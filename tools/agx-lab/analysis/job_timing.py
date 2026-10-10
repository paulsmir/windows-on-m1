# Decode Wom1JobTiming971 (ADMISSION_JOB_TIMING_STATE v1). argv: bin...
import struct, sys, statistics
names = ['Submit','Worker','BeginBefore','BeginAfter','BackendBefore','KickTa','Kick3d','BackendAfter','FirstProgress','Complete','JobEnd','Notify']
for path in sys.argv[1:]:
    b = open(path,'rb').read()
    ver, size, count, _ = struct.unpack_from('<4I', b, 0)
    freq, _last = struct.unpack_from('<2Q', b, 16)
    slot_fmt = '<4I2Q12Q4Q2I'; ssz = struct.calcsize(slot_fmt)
    slots = []
    for i in range(64):
        v = struct.unpack_from(slot_fmt, b, 32 + i*ssz)
        if v[0] and all(v[6+k] for k in (0,1,9,11)): slots.append(v)
    slots.sort(key=lambda v: v[6])
    print(path.split('/')[-2], 'ver', ver, 'count', count, 'freq', freq, 'complete slots', len(slots))
    def us(v,a,c): return (v[6+c]-v[6+a])*1e6/freq if v[6+a] and v[6+c] else None
    pairs = [(0,1),(1,2),(2,3),(3,4),(4,7),(7,9),(9,10),(10,11),(0,11)]
    for a,c in pairs:
        d=[x for x in (us(v,a,c) for v in slots) if x is not None and x>=0]
        if d: print(f'  {names[a]:>13}->{names[c]:<13} median {statistics.median(d):8.1f} us  p90 {sorted(d)[int(len(d)*0.9)-1 if len(d)>1 else 0]:8.1f}  n={len(d)}')
    fw=[(v[19]-v[18], v[21]-v[20]) for v in slots if v[22]]
    if fw: print('  firmware TA ticks median', statistics.median(x[0] for x in fw), '3D', statistics.median(x[1] for x in fw))
    gaps=[(slots[i+1][6]-slots[i][6+11])*1e6/freq for i in range(len(slots)-1)]
    if gaps: print('  Notify(i)->Submit(i+1) median', round(statistics.median(gaps),1), 'us')
