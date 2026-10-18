"""Opt-in C19 actual-owner coordinate gate; no compiler without parent BEFORE."""
import hashlib,json,os,subprocess,unittest
from pathlib import Path
from test_d3d12_view_mapping import block
ROOT=Path(__file__).resolve().parents[1]
PINS=ROOT/'tests/d3d12_pixel_center_pins.json'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class D3d12PixelCenter(unittest.TestCase):
 def test_actual_owner_and_asahi_preserve_half_integer_position(self):
  target=os.environ.get('AGX_C19_CENTER_RECEIPTS')
  if not target:self.skipTest('C19 coordinate compiler/run requires parent BEFORE and approved manifest')
  approval=Path(os.environ['AGX_C19_CENTER_APPROVED_MANIFEST']);self.assertEqual(sha(approval),os.environ['AGX_C19_CENTER_APPROVED_SHA256'])
  approved=json.loads(approval.read_text());self.assertEqual(sha(PINS),approved['pins_sha256'])
  for name,expected in approved['sources'].items():self.assertEqual(sha(ROOT/name),expected,name)
  pins=json.loads(PINS.read_text())
  for name,item in pins['files'].items():
   p=Path(name);self.assertEqual(p.stat().st_size,item['bytes'],name);self.assertEqual(str(p.resolve()),item['resolved_path'],name);self.assertEqual(sha(p),item['sha256'],name)
  out=Path(target);out.mkdir(parents=True,exist_ok=True);self.assertFalse((out/'results.json').exists(),'do not overwrite prior attempts')
  (out/'approved-inputs.json').write_bytes(approval.read_bytes());(out/'pins.json').write_bytes(PINS.read_bytes())
  owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
  actual=block(owner,'static void *create_shader(')
  source=out/'actual-owner-center.cpp';source.write_text((ROOT/'tests/d3d12_pixel_center_fixture.cpp').read_text().replace('/* ACTUAL_OWNER_CREATE_SHADER */',actual))
  flags=pins['host_flags'];driver=ROOT/'drivers/apple-agx/mesa/dxbc';mesa=Path(pins['mesa']);build=Path(pins['build'])
  includes=['-I',str(driver),'-I',str(ROOT/'drivers/apple-agx/windows/icd'),'-I',str(mesa/'src/gallium/include'),'-I',str(build/'src/asahi/genxml'),'-I',str(build/'src/asahi'),'-I',str(mesa/'src/asahi')]
  instrumentation=['-g','-O1','-fsanitize=address,undefined'];commands=[];objects=[]
  for path,name in [(driver/'dxbc_parse.c','parse.o'),(driver/'dxbc_to_nir.c','dxbc.o'),(mesa/'src/asahi/lib/agx_nir_lower_sample_intrinsics.c','sample.o')]:
   obj=out/name;objects.append(obj);commands.append(['/usr/bin/clang','-std=c11','-Werror=implicit-function-declaration',*instrumentation,*flags,*includes,'-c',str(path),'-o',str(obj)])
  objects.append(out/'owner.o');commands.append(['/usr/bin/clang++','-std=c++17','-Werror=sign-compare',*instrumentation,*flags,*includes,'-c',str(source),'-o',str(objects[-1])])
  commands.append(['/usr/bin/clang++',*instrumentation,*map(str,objects),*pins['libraries'],'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread','-o',str(out/'center-check')])
  commands.append([str(out/'center-check')]);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  (out/'generated-input.json').write_text(json.dumps({'actual-owner-center.cpp':sha(source)},indent=2)+'\n');results=[]
  for i,cmd in enumerate(commands):
   r=subprocess.run(cmd,cwd=out,capture_output=True,text=True,timeout=30);(out/f'{i}-stdout.txt').write_text(r.stdout);(out/f'{i}-stderr.txt').write_text(r.stderr);results.append({'step':i,'returncode':r.returncode});(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertIn('C19_PIXEL_CENTER_PASS',r.stdout);print(r.stdout.strip())
  (out/'artifacts.json').write_text(json.dumps({p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in out.iterdir() if p.is_file()},indent=2)+'\n')
