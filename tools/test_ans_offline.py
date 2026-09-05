#!/usr/bin/env python3
"""ANS-only test runner: isolated outputs, host sanitizers and ARM64 objects.

This never installs, signs, opens a serial device or launches a guest.
A GREEN receipt describes helper tests, not a complete hardware candidate.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.local/ans-native/tests'

def run(argv):
    p=subprocess.run(argv,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    print(p.stdout,end='')
    if p.returncode: raise RuntimeError(f'gate failed: {argv[0]} (exit {p.returncode})')
    return p.stdout

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    run([sys.executable,'tools/generate_j313_ans_contract.py','--check'])
    run([sys.executable,'-m','unittest','tests.test_j313_ans_contract','-q'])
    cc=shutil.which('cc')
    clang=shutil.which('clang',path='/opt/homebrew/opt/llvm/bin:'+os.environ.get('PATH',''))
    records=[]
    for name in ('hv_ans_broker','hv_ans_prp'):
        exe=OUT/name
        args=[cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
              '-I','m1n1_windows/src',f'm1n1_windows/tests/{name}_test.c',f'm1n1_windows/src/{name}.c','-o',str(exe)]
        run(args); result=run([str(exe)])
        obj=OUT/(name+'.o')
        resource=run([clang,'-print-resource-dir']).strip()
        run([clang,'--target=aarch64-none-elf','-std=c11','-Wall','-Wextra','-Werror',
             '-ffreestanding','-nostdinc','-isystem',resource+'/include',
             '-isystem','m1n1_windows/sysinc','-I','m1n1_windows/src',
             '-c',f'm1n1_windows/src/{name}.c','-o',str(obj)])
        records.append(dict(test=name,result=result.strip(),arm64_object=str(obj.relative_to(ROOT)),
                            sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))
    receipt=dict(scope='offline helper gates only',hardware_candidate_ready=False,tests=records)
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('ANS helper gates GREEN; hardware candidate NOT admitted')

if __name__=='__main__': main()
