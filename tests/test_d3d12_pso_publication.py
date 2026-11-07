"""Opt-in actual-function C15 PSO publication regression, derived from EXP1409.

No native hardware or external compiler library: actual DDI compile/publication/
destroy functions execute against original counting constructors. The original
six numeric cases and causal A/B schedule remain unchanged. Parent must freeze
source/tool/fixture approvals and register a BEFORE before enabling this test.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
from d3d12_pso_publication_source import ROOT, render

HERE=Path(__file__).resolve().parent
PINS=HERE/'d3d12_pso_publication_pins.json'


class PsoPublication(unittest.TestCase):
    def test_one_complete_native_tuple_and_terminal_result_per_pso(self):
        receipt_root=os.environ.get('AGX_PSO_PUBLICATION_RECEIPTS')
        if not receipt_root:
            self.skipTest('explicit parent BEFORE and pinned source/tool approval required')
        pins=json.loads(PINS.read_text())
        before=os.environ.get('AGX_PSO_PUBLICATION_AUTHORIZED_BEFORE')
        self.assertEqual(pins['schema'],1)
        self.assertNotEqual(pins['approved_before'],'PENDING_PARENT_GREEN_BEFORE')
        self.assertEqual(before,pins['approved_before'],'missing/mismatched preregistered BEFORE')
        # Fail closed before compiler or --version if any source/fixture/tool
        # input differs. Approval includes this test and renderer themselves.
        observed={}
        for relative,wanted in pins['approved_files'].items():
            path=ROOT/relative
            actual=hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(actual,wanted,relative)
            observed[str(path)]=actual
        compiler=Path(pins['compiler']['path'])
        actual_compiler=compiler.resolve(strict=True)
        self.assertEqual(str(actual_compiler),pins['compiler']['resolved'])
        self.assertEqual(hashlib.sha256(actual_compiler.read_bytes()).hexdigest(),pins['compiler']['sha256'])
        sdk=Path(pins['compiler']['sdk'])
        self.assertEqual(hashlib.sha256((sdk/'SDKSettings.json').read_bytes()).hexdigest(),
                         pins['compiler']['sdk_settings_sha256'])
        out=Path(receipt_root)/'green'
        self.assertFalse(out.exists(),'refuse to overwrite previous experiment receipts')
        source=render(out)
        rendered=json.loads((out/'inputs.json').read_text())
        self.assertEqual(rendered['actual_functions'],pins['approved_functions'])
        (out/'approval.json').write_text(json.dumps({'before':before,'pins_sha256':hashlib.sha256(PINS.read_bytes()).hexdigest(),'approved_input_sha256':observed,'compiler_path':str(compiler),'compiler_realpath':str(actual_compiler),'compiler_sha256':pins['compiler']['sha256']},indent=2)+'\n')
        commands=[ [str(compiler),'--version'], [str(compiler),'-std=c++17','-g','-O1',
            '-fsanitize=address,undefined','-isysroot',str(sdk),'-pthread','-I',str(ROOT/'drivers/apple-agx/mesa/dxbc'),
            str(source),'-o',str(out/'pso-publication')] ]
        commands += [[str(out/'pso-publication'),kind,mode]
                     for kind in ['compute','graphics'] for mode in ['serial','parallel','failure']]
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        results=[]
        for i,command in enumerate(commands):
            with self.subTest(step=i,argv=command):
                try:
                    run=subprocess.run(command,capture_output=True,text=True,timeout=30)
                except subprocess.TimeoutExpired as error:
                    (out/f'{i}-timeout.txt').write_text(str(error))
                    self.fail('bounded fixture/compiler timeout')
                (out/f'{i}-stdout.txt').write_text(run.stdout)
                (out/f'{i}-stderr.txt').write_text(run.stderr)
                results.append({'step':i,'command':command,'returncode':run.returncode})
                (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
                if i==0:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual(run.stdout,pins['compiler']['version_stdout'])
                elif i==1:
                    # Compile failure is never counted as a publication RED.
                    if run.returncode:
                        self.fail('HOST_COMPILE_FAILED: '+run.stderr)
                        return
                else:
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                    self.assertIn('calls=1',run.stdout)
                    self.assertIn('noLeak=1 freshPSO=1',run.stdout)
                    print(run.stdout.strip())
            # Do not execute a missing fixture after an infrastructure failure.
            if i<2 and (run.returncode or (i==0 and run.stdout!=pins['compiler']['version_stdout'])):
                break
        manifest={str(path.relative_to(out)):{'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                  for path in sorted(out.rglob('*')) if path.is_file() and path.name!='output-manifest.json'}
        (out/'output-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
