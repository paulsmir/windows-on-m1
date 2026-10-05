from pathlib import Path
import ctypes as C, hashlib, json, re, struct
p=Path(__file__).parent
d=json.loads((p/'R154-native-graph-decoded.json').read_text())
s=(p/'R154-native-envelope.txt').read_text(); mem={}
for l in s.splitlines():
 m=re.match(r'([0-9a-f]{8}`[0-9a-f]{8})  ((?:[0-9a-f]{8} ?){1,4})$',l)
 if m:
  a=int(m[1].replace('`',''),16)
  for i,w in enumerate(m[2].split()):mem[a+4*i]=int(w,16)
header=b''.join(struct.pack('<I',mem[0xffff9b0acef7e8e0+i]) for i in range(0,168,4))
command=(p/'R154-native-command.bin').read_bytes(); data=C.create_string_buffer(header+command)
ranges=[struct.unpack_from('<QII',header,24+16*i) for i in range(9)]
def walk(va,write):
 t=0x9d5a38000
 for level,idx in enumerate(((va>>36)&7,(va>>25)&2047,(va>>14)&2047)):
  n=next((n for n in d['parents' if level<2 else 'leaves'] if n['ipa']==t and n['index']==idx),None)
  if n is None:return False
  t=n['aux']
 return bool(t and (not write or n['writable']))
def field(name):
 match=re.search(r'\b'+name+r'\s+: (0x[0-9a-f]+(?:`[0-9a-f]+)?|[0-9]+)',s)
 assert match, name
 return int(match[1].replace('`',''),0)
pte_valid=(field('GuestIpa')!=0 and field('Flags')&1 and not field('Flags')&~3 and field('SegmentId')<=2)
checks=[]
CALLBACK=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_ulonglong,C.c_uint,C.c_int,C.c_int,C.c_uint)
@CALLBACK
def access(_ctx,va,size,write,kind,ordinal):
 if kind==2:
  # Exact single logical page in R154-native-envelope.txt; all requested bytes fit.
  result=(0x3d950000<=va and va+size<=0x3d951000 and not write and pte_valid)
 elif kind==1:
  result=(ordinal<9 and (va,size,0)==ranges[ordinal] and write and all(walk(x,True) for x in range(va&~0x3fff,va+size,0x4000)))
 else:
  result=not (va<0x4000000 and va+size>0x2000000) and all(walk(x,write) for x in range(va&~0x3fff,va+size,0x4000))
 checks.append(dict(va=hex(va),bytes=size,write=write,kind=kind,ordinal=ordinal,valid=bool(result)))
 return int(result)
lib=C.CDLL(str(p/'R154-native-parser.dylib'));fn=lib.AppleAgxG4ParseSubmitEx
fn.argtypes=[C.c_void_p,C.c_uint,C.c_uint,C.c_ulonglong,C.c_uint,CALLBACK,C.c_void_p,C.c_void_p,C.c_void_p];fn.restype=C.c_int
view=C.create_string_buffer(512);failure=C.create_string_buffer(64)
result=fn(data,448,448,0x3d950000,280,access,None,view,failure)
out=dict(source_commit='335031768e44505d33fcbc91b4899fdf13b8fb60',parse_result=result,parse_ok=result==0,access_checks=checks,limitation='Offline exact production parser with dump graph callback; excludes live lease/firmware operation and pre-TDR timing.')
out['sha256']={name:hashlib.sha256((p/name).read_bytes()).hexdigest() for name in ['apple_agx_g4_submit.c','apple_agx_g4_submit.h','R154-native-command.bin','R154-native-graph.txt','R154-native-graph-decoded.json','R154-native-envelope.txt','R154-native-replay.py','R154-native-parser.dylib']}
(p/'R154-native-parser-result.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
assert result==0
