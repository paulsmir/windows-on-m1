# Decode Wom1G3PrivateProcs.bin (per-process private pool, EXP1114 layout). argv: bin...
# Layout: header 8 words, 16 slots x 16 words.
import struct,sys
for path in sys.argv[1:]:
    b=open(path,'rb').read(); w=struct.unpack('<%dI'%(len(b)//4),b)
    print(path); print(' version %d slots %d global %d units largest-free %d processes %d'%(w[0],w[1],w[2],w[3],w[7]))
    print('  pid   units scenes/cached maxTVB shape      heapBlk valid max_pages max_b overflow gpu_c reset pages blocks ctl.total ctl.wptr')
    for i in range(w[1]):
        r=w[8+16*i:8+16*i+16]
        print('  %5d %5d %3d/%-3d %6d %4dx%-5d %6d %4d %9d %5d %8d %5d %5d %5d %6d %6d %6d'%(r[0],r[1],r[2]&0xffff,r[2]>>16,r[3],r[4]&0xffff,r[4]>>16,r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15]))
