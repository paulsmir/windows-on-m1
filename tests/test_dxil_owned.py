"""EXP1396 original owned adapter + existing exact external C API archives.

Opt-in only: parent preregisters compiler/run; never rebuilds a dependency.
"""
import hashlib,json,os,subprocess,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXTERNAL=Path('~/public_windows/.local/c15-host-8434b063').expanduser()
ARCHIVES=['libdxil-spirv-c-static.a','libdxil-converter.a','libspirv-module.a','bc/libllvm-bc.a','third_party/libglslang-spirv-builder.a','third_party/libbc-decoder.a','libdxil-utils.a','libdxil-debug.a','third_party/libdxbc-spirv.a']
class DxilOwned(unittest.TestCase):
 def test_actual_capi_owned_output_and_reported_failure_lifetime(self):
  receipt_dir=os.environ.get('AGX_C15_OWNED_RECEIPTS')
  if not receipt_dir:self.skipTest('EXP1396 compiler/run requires parent preregistration and explicit receipt directory')
  phase=os.environ.get('AGX_C15_OWNED_PHASE','green')
  self.assertRegex(phase,r'^[a-z0-9-]+$')
  out=Path(receipt_dir)/phase;out.mkdir(parents=True,exist_ok=True)
  driver=ROOT/'drivers/apple-agx/mesa/dxil';fixture=ROOT/'tests/fixtures/dxil/exp1389_cs60.dxil'
  self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(),'f3934fccef3b1428d62ed6a675540cab1bf674faaf85c67320e3ed83c6c62bb4')
  libs=[EXTERNAL/'build'/p for p in ARCHIVES]
  files=[driver/'agx_dxil_program.c',driver/'agx_dxil_program.h',driver/'agx_dxil_owned.c',driver/'agx_dxil_owned.h',ROOT/'drivers/apple-agx/mesa/dxbc/dxbc_bindings.h',ROOT/'tests/dxil_owned_fixture.c',Path(__file__).resolve(),EXTERNAL/'source/dxil_spirv_c.h',fixture,*libs]
  hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else 'ABSENT' for p in files}
  (out/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
  flags=['-g','-O1','-fsanitize=address,undefined']
  objects=[out/'program.o',out/'owned.o',out/'fixture.o'];commands=[]
  for source,obj in zip([driver/'agx_dxil_program.c',driver/'agx_dxil_owned.c',ROOT/'tests/dxil_owned_fixture.c'],objects):
   commands.append(['clang','-std=c11','-Wall','-Wextra','-Werror',*flags,'-I',str(driver),'-I',str(EXTERNAL/'source'),'-c',str(source),'-o',str(obj)])
  commands.append(['clang++',*flags,*map(str,objects),*map(str,libs),'-o',str(out/'owned-check')])
  commands.append([str(out/'owned-check'),str(fixture)])
  (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  results=[]
  for i,command in enumerate(commands):
   result=subprocess.run(command,capture_output=True,text=True,timeout=30)
   (out/f'{i}-stdout.txt').write_text(result.stdout);(out/f'{i}-stderr.txt').write_text(result.stderr)
   results.append({'step':i,'returncode':result.returncode})
   (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  self.assertIn('C15_OWNED_CONVERTER_PASS one_key=YES',result.stdout)
  print(result.stdout.strip())
