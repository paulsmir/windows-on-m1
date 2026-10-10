# Decode Wom1SubmitPath1135 (v1): Version Bytes Immediate Deferred Binds Reserved Waits[4] WaitQpc[4] MaxWaitQpc
import struct, sys
for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    v = struct.unpack_from('<2I4i4i4qq', b, 0)
    ver, size, imm, dfr, binds, _r = v[0], v[1], v[2], v[3], v[4], v[5]
    waits = v[6:10]; wq = v[10:14]; mx = v[14]
    f = 24e6
    names = ['backend-submitted', 'worker-finishing', 'slot-occupied', 'other']
    print(p.split('/')[-2], f'immediate {imm} deferred {dfr} binds {binds}  max wait {mx/f*1e3:.2f} ms')
    for n, c, q in zip(names, waits, wq):
        print(f'   wait {n:18} n={c:6d} total {q/f*1e3:8.1f} ms  avg {q/f*1e6/max(c,1):7.1f} us')
