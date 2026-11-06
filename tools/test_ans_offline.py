#!/usr/bin/env python3
"""ANS-only test runner: isolated outputs, host sanitizers and ARM64 objects.

This never installs, signs, opens a serial device or launches a guest.
A GREEN receipt describes offline contract tests, not hardware qualification.
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
COMMANDS=[]

def run(argv, cwd=ROOT):
    p=subprocess.run(argv,cwd=cwd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    print(p.stdout,end='')
    COMMANDS.append(dict(argv=argv, cwd=str(cwd), exit_code=p.returncode,
                         output_sha256=hashlib.sha256(p.stdout.encode()).hexdigest()))
    if p.returncode: raise RuntimeError(f'gate failed: {argv[0]} (exit {p.returncode})')
    return p.stdout

def persist_receipt(out, receipt):
    """Keep ledger targets immutable; receipt.json is only a latest-run convenience."""
    data=(json.dumps(receipt,indent=2)+'\n').encode()
    archive=Path(out)/'receipts'/(hashlib.sha256(data).hexdigest()+'.json')
    archive.parent.mkdir(parents=True,exist_ok=True)
    try:
        with archive.open('xb') as stream: stream.write(data)
    except FileExistsError:
        if archive.read_bytes()!=data:
            raise RuntimeError('existing receipt archive does not match its content hash')
    (Path(out)/'receipt.json').write_bytes(data)
    return archive

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'receipt.json').unlink(missing_ok=True)
    run([sys.executable,'tools/generate_j313_ans_contract.py','--check'])
    run([sys.executable,'-m','unittest','tests.test_j313_ans_contract','tests.test_ans_launcher_mode',
         'tests.test_ans_qualification','tests.test_ans_launch_lock','tests.test_assisted_session',
         'tests.test_ans_gate_receipts','tests.test_ans_dart_census', 'tests.test_ans_mu_broker_window',
         'tests.test_public_scripts','tests.test_guest_layout_generation','tests.test_launch_profile',
         'tests.test_virtual_display','tests.test_launch_contract_proxy',
         'tests.test_ans_raw_pattern','-q'])
    cc=shutil.which('cc')
    clang=shutil.which('clang',path='/opt/homebrew/opt/llvm/bin:'+os.environ.get('PATH',''))
    run([sys.executable,'tools/test_ans_memory.py'])
    run([sys.executable,'tools/test_ans_private_mmio.py'])
    for test in ('nvme_prp_quarantine_test.py', 'nvme_native_lifecycle_test.py',
                 'rtkit_native_lifecycle_test.py', 'hv_ans_telemetry_mmio_test.py',
                 'hv_ans_handoff_memory_test.py'):
        run([sys.executable, 'm1n1_windows/tests/'+test])
    run(['sh','drivers/storage/appleans/common/tests/run.sh'])
    run(['sh','drivers/storage/appleans/windows/tests/run.sh'])
    run(['sh','mu/Silicon/Apple/AppleSiliconPkg/Library/AppleAnsExitBootServicesGateLib/Tests/run.sh'])
    run(['sh','tests/run_host_tests.sh','hv_exception_lower_test',
         'hv_irq_routes_test','hv_pci_state_test','hv_stage2_state_test',
         'hv_fiq_fast_path_test','hv_launch_j313_test','ans_queue_test'], cwd=ROOT/'m1n1_windows')
    records=[]
    for name, sources in {
        'ans_queue': ['ans_queue'],
        'hv_ans_pins': ['hv_ans_pins'],
        'hv_ans_service': ['hv_ans_service', 'hv_ans_broker', 'hv_ans_prp'],
        'hv_ans_memory': ['hv_ans_memory', 'hv_ans_prp'],
    }.items():
        exe=OUT/name
        run([cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I','m1n1_windows/src',f'm1n1_windows/tests/{name}_test.c',
             *[f'm1n1_windows/src/{source}.c' for source in sources],'-o',str(exe)])
        result=run([str(exe)])
        records.append(dict(test=name,result=result.strip()))
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
    receipt=dict(scope='offline ANS001 contract gates; full V1 DMA not admitted',
                 passed=True,hardware_candidate_ready=False,native_dma_admitted=False,
                 tests=records,commands=COMMANDS)
    archive=persist_receipt(OUT,receipt)
    print('Immutable gate receipt:',archive.relative_to(ROOT))
    print('ANS offline contract gates GREEN; hardware candidate NOT admitted')

if __name__=='__main__': main()
