"""Original actual-branch selector. Data-only source generation, no native code execution."""
import argparse,ast,difflib,hashlib,json,pathlib,re
P=pathlib.Path
ROOT=P(__file__).resolve().parents[1]
PROJECTOR=ROOT/'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'
PROFILES={
 'mesa-9aa1215f-pristine':('5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba',
                          'f94f37ebfa8e10d00a219362049878bcd7ab9e96eb0e6397100e94944f0aacac'),
 'qualified-windows-1469':('9b3d61fd2875067ba8adcf268ff4c78c684e2c266f84c64de48bb4674ec83723',
                            'a967fe9dcce4ba957a73a7f0e2b2640aade0ea22c198d981502a3109066f4187'),
}
def sha(raw):return hashlib.sha256(raw).hexdigest()
def closing(text,start):
 depth=0;i=start;mode='code'
 while i<len(text):
  c=text[i];n=text[i:i+2]
  if mode=='line':
   if c=='\n':mode='code'
  elif mode=='block':
   if n=='*/':mode='code';i+=1
  elif mode in ('string','char'):
   if c=='\\':i+=1
   elif c==('"'if mode=='string'else"'"):mode='code'
  elif n=='//':mode='line';i+=1
  elif n=='/*':mode='block';i+=1
  elif c=='"':mode='string'
  elif c=="'":mode='char'
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if depth==0:return i+1
  i+=1
 raise ValueError('UNBALANCED_ACTUAL_FUNCTION')
def body(text,name,signature):
 matches=list(re.finditer(signature+re.escape(name)+r'\([^;{}]*\)\s*\{',text))
 if len(matches)!=1:raise ValueError('ACTUAL_FUNCTION_COUNT '+name)
 start=matches[0].start();brace=text.index('{',matches[0].start());end=closing(text,brace)
 return start,brace,end

def prefix(text,name):
 start,brace,end=body(text,name,r'static void \*\s*\n')
 actual=text[start:end];allocation='rzalloc(NULL, struct agx_uncompiled_shader);'
 if actual.count(allocation)!=1:raise ValueError('EXACT_FIRST_SHADER_ALLOCATION')
 alloc=actual.index(allocation);guard=actual.find('   if (!so)',alloc)
 if guard<0:raise ValueError('MISSING_EARLY_GUARD')
 tail=actual[guard:]
 if tail.startswith('   if (!so)\n      return NULL;'):
  boundary=guard+len('   if (!so)\n      return NULL;')
 elif tail.startswith('   if (!so) {'):
  boundary=closing(actual,actual.index('{',guard))
 else:raise ValueError('UNSUPPORTED_ACTUAL_GUARD_SHAPE')
 selected=actual[:boundary]
 # No other statement/call is silently skipped inside the actual selected prefix.
 if selected.count(allocation)!=1 or 'return NULL;'not in selected:raise ValueError('SELECTED_BOUNDARY')
 return selected+'\n   (void)ctx;\n   (void)dev;\n   ownership_forbidden_continuation();\n}\n'

def render(text):
 license=text[:text.index('#include')]
 definitions='\n'.join(prefix(text,n)for n in ['agx_create_shader_state','agx_create_compute_state'])
 _,brace,end=body(text,'agx_init_state_functions',r'void\s*\n')
 init=text[brace:end];lines=[]
 for field,target in [('create_vs_state','agx_create_shader_state'),('create_fs_state','agx_create_shader_state'),('create_gs_state','agx_create_shader_state'),('create_tcs_state','agx_create_shader_state'),('create_tes_state','agx_create_shader_state'),('create_compute_state','agx_create_compute_state')]:
  matches=re.findall(r'^   ctx->'+field+r' = '+target+r';$',init,re.M)
  if len(matches)!=1:raise ValueError('ACTUAL_CALLBACK_ASSIGNMENT '+field)
  lines.append(matches[0])
 return license+definitions+'\nstatic void ownership_install(struct pipe_context *ctx)\n{\n'+'\n'.join(lines)+'\n}\n'

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--state',required=True);ap.add_argument('--profile',choices=PROFILES,default='mesa-9aa1215f-pristine');ap.add_argument('--projector-sha',required=True);ap.add_argument('--output',required=True);a=ap.parse_args()
 state_sha,green_full_sha=PROFILES[a.profile]
 raw=P(a.state).read_bytes();projector=PROJECTOR.read_bytes()
 if sha(raw)!=state_sha or sha(projector)!=a.projector_sha:raise ValueError('EXACT_CURRENT_PROJECTOR_AND_SOURCE_PINS')
 original=raw.decode('utf-8').replace('\r\n','\n');tree=ast.parse(projector.decode())
 nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='project_constructor_nir_consumption']
 if len(nodes)!=1:raise ValueError('EXACT_ORIGINAL_PROJECTOR_HELPER')
 ns={};exec(compile(ast.Module(body=nodes,type_ignores=[]),str(PROJECTOR),'exec'),ns)
 green=ns['project_constructor_nir_consumption'](original)
 if sha(green.encode())!=green_full_sha:raise ValueError('ROOT_KNOWN_GREEN_FULL_SOURCE')
 changes=[v for v in difflib.ndiff(original.splitlines(True),green.splitlines(True))if v.startswith(('- ','+ '))]
 removals=[v[2:]for v in changes if v.startswith('- ')];additions=[v[2:]for v in changes if v.startswith('+ ')]
 if removals!=['   if (!so)\n']*2 or additions!=['   if (!so) {\n','      if (cso->type == PIPE_SHADER_IR_NIR)\n','         ralloc_free(cso->ir.nir);\n','   }\n','   if (!so) {\n','      if (cso->ir_type == PIPE_SHADER_IR_NIR)\n','         ralloc_free((void *)cso->prog);\n','   }\n']:raise ValueError('EXACT_TWO_GUARD_DIFF_REQUIRED')
 if original[:original.index('#include')]!=green[:green.index('#include')]:raise ValueError('LICENSE_PREFIX_CHANGED')
 out=P(a.output);out.mkdir(exist_ok=False);files={}
 for kind,text in [('red',original),('green',green)]:
  d=out/kind;d.mkdir();p=d/'constructors.inc';p.write_text(render(text));files[kind+'/constructors.inc']={'Bytes':p.stat().st_size,'SHA256':sha(p.read_bytes())}
 proof={'status':'EXACT_TWO_EARLY_GUARDS_ONLY_SOURCE_VIEW','profile':a.profile,'CURRENT_projector':str(PROJECTOR),'CURRENT_projector_sha256':a.projector_sha,'red_raw_sha256':state_sha,'green_normalized_full_sha256':green_full_sha,'license_prefix_identical':True,'remainder_diff_is_only_two_early_guards':True,'callback_assignments':6,'forbidden_continuation':'TRAP_ONLY_NO_SUCCESS_STUB_OR_FAKE_COMPILER','files':files}
 (out/'selector-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
 print(json.dumps(proof))
if __name__=='__main__':main()
