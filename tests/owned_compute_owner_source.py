"""Original exact full-owner source selector; native real-WDK fixture only.
No command is run or external file read on import. No private fallback."""
import argparse,hashlib,json,pathlib,re
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def end_brace(s,start):
 depth=0;mode='code';i=start
 while i<len(s):
  c=s[i];two=s[i:i+2]
  if mode=='line':
   if c=='\n':mode='code'
  elif mode=='comment':
   if two=='*/':mode='code';i+=1
  elif mode in ('string','char'):
   if c=='\\':i+=1
   elif c==('"'if mode=='string'else"'"):mode='code'
  elif two=='//':mode='line';i+=1
  elif two=='/*':mode='comment';i+=1
  elif c=='"':mode='string'
  elif c=="'":mode='char'
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if depth==0:return i+1
  i+=1
 raise ValueError('ACTUAL_SOURCE_UNBALANCED')
def function(s,name):
 pat=r'(?:static bool|extern "C" (?:HRESULT|void))\s+'+re.escape(name)+r'\([^;{}]*\)\s*\{'
 m=list(re.finditer(pat,s))
 if len(m)!=1:raise ValueError('ACTUAL_FUNCTION_COUNT '+name)
 begin=m[0].start();brace=s.index('{',begin);return s[begin:end_brace(s,brace)]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--source-root',required=True);ap.add_argument('--owner-sha',required=True);ap.add_argument('--header-sha',required=True);ap.add_argument('--baseline-owner',required=True);ap.add_argument('--baseline-sha',required=True);ap.add_argument('--out',required=True);a=ap.parse_args()
 root=pathlib.Path(a.source_root).resolve();owner=root/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp';header=owner.with_suffix('.h');base=pathlib.Path(a.baseline_owner)
 if sha(owner)!=a.owner_sha or sha(header)!=a.header_sha or sha(base)!=a.baseline_sha:raise ValueError('ACTUAL_SOURCE_PINS')
 s=owner.read_text();old=base.read_text()
 for name in ['AgxD3d12OwnerCreateCompute','AgxD3d12OwnerDestroyCompute','AgxD3d12OwnerDispatch']:
  if function(s,name)!=function(old,name):raise ValueError('OLD_OWNER_BODY_CHANGED '+name)
 def structure(t):
  start=t.index('struct AGX_D3D12_OWNER {');return t[start:end_brace(t,t.index('{',start))+1]
 if structure(s)!=structure(old):raise ValueError('ACTUAL_OWNER_TYPE_CHANGED')
 boundary=s.index('/* C7b: a presentation texture')
 prefix=s[:boundary]
 if prefix.count('struct AGX_D3D12_OWNER {')!=1:raise ValueError('EXACT_OWNER_TYPE_PREFIX')
 names=['compute_prepared_empty','AgxD3d12OwnerCreateComputePrepared','AgxD3d12OwnerDestroyCompute']
 selected=prefix+'\n#if !defined(APPLE_AGX_D3D12_PREPARED_NIR_OWNER) || !APPLE_AGX_D3D12_PREPARED_NIR_OWNER\n#error Qualified native owner fixture requires APPLE_AGX_D3D12_PREPARED_NIR_OWNER=1\n#endif\n'+'\n\n'.join(function(s,n)for n in names)+'\n'
 out=pathlib.Path(a.out);out.mkdir(exist_ok=False);q=out/'owner_selected.inc';q.write_text(selected)
 proof={'status':'DATA_ONLY_ACTUAL_COMPLETE_FUNCTIONS_AND_OWNER_PREFIX','owner_sha':a.owner_sha,'header_sha':a.header_sha,'baseline_sha':a.baseline_sha,'old_create_destroy_dispatch_byte_unchanged':True,'actual_owner_type_byte_unchanged':True,'functions':names,'required_actual_compile_define':'APPLE_AGX_D3D12_PREPARED_NIR_OWNER=1','selected':{'bytes':q.stat().st_size,'sha256':sha(q)},'limits':'Actual native Windows types/headers required; typed fixture owner with CPU callbacks is not real Windows/Asahi initialization'}
 (out/'owner-source-proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
