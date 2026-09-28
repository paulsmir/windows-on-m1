from pathlib import Path
import hashlib,json,tarfile
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next');base=Path(r'C:\Users\pauls\R149-offline')
before=json.loads((base/'builder-before-hashes.json').read_text());final=json.loads((base/'final-source-hashes.json').read_text())
for n,h in before.items():assert hashlib.sha256((root/n).read_bytes()).hexdigest()==h,n
with tarfile.open(base/'changed-source.tar') as t:
 names=t.getnames()
 assert all(n in final and not Path(n).is_absolute() and '..' not in Path(n).parts for n in names)
 t.extractall(root)
for n,h in final.items():assert hashlib.sha256((root/n).read_bytes()).hexdigest()==h,n
print('Verified',len(before),'before; applied',len(names),'changed files; final',len(final),'exact')
