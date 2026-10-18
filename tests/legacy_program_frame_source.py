"""Original current-module PROGRAM fixture renderer; no compiler invocation."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'drivers/apple-agx/mesa/dxbc'
CC=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang')
CC_SHA='1590ac950a3d627817d09ade5cb60b2115f17a72182a3141e010b4bcc482a0c9'
NM=Path('/usr/bin/nm')
SDK=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
NAMES=['vs40','ps40','gs40','ps41','cs40','cs41','cs50','hs50','ds50','ps50','ps51','cs51static']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def inputs():
 return [D/n for n in ['agx_legacy_program_frame.c','agx_legacy_program_frame.h','dxbc_parse.c','dxbc_parse.h','dxbc_bindings.h']]+[ROOT/'tests'/n for n in ['legacy_program_frame_fixture.c','legacy_program_frame_source.py','test_legacy_program_frame.py']]+[ROOT/'tests/fixtures/dxbc'/(n+'.dxbc')for n in NAMES]
def commands(receipts,generated):
 o,g=Path(receipts).resolve(),Path(generated).resolve();flags=['-std=c11','-isysroot',str(SDK),'-fsanitize=address,undefined','-fno-omit-frame-pointer']
 return [[str(CC),*flags,'-I',str(D),'-c',str(D/'agx_legacy_program_frame.c'),'-o',str(o/'framer.o')],[str(NM),'-u',str(o/'framer.o')],[str(CC),*flags,'-I',str(D),str(ROOT/'tests/legacy_program_frame_fixture.c'),str(D/'dxbc_parse.c'),str(o/'framer.o'),'-o',str(o/'fixture')],[str(o/'fixture'),*[str(g/(n+'.program'))for n in NAMES]]]
def render(out):
 out=Path(out);out.mkdir(exist_ok=False);rows=[]
 for n in NAMES:
  p=ROOT/'tests/fixtures/dxbc'/(n+'.dxbc');data=p.read_bytes();u=lambda x:int.from_bytes(data[x:x+4],'little');assert data[:4]==b'DXBC'and u(24)==len(data);programs=[]
  for i in range(u(28)):
   at=u(32+4*i);length=u(at+4);assert at+8+length<=len(data)
   if data[at:at+4]in [b'SHDR',b'SHEX']:programs.append(data[at+8:at+8+length])
  assert len(programs)==1 and len(programs[0])>=8;program=programs[0];assert int.from_bytes(program[4:8],'little')*4==len(program);q=out/(n+'.program');q.write_bytes(program);rows.append({'name':n,'source':str(p),'source_sha256':sha(p),'program_sha256':sha(q),'program_bytes':len(program)})
 (out/'provenance.json').write_text(json.dumps(rows,indent=2)+'\n');m={'source_inputs':{str(p):{'sha256':sha(p),'bytes':p.stat().st_size}for p in inputs()},'generated':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size}for p in out.iterdir()if p.is_file()}};(out/'generated-inputs.json').write_text(json.dumps(m,indent=2)+'\n');return m
