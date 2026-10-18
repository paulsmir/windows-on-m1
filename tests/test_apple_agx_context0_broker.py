from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SHARED=ROOT/'drivers/apple-agx/shared'
M1N1=ROOT/'m1n1_windows'
if not (M1N1/'src/hv_agx_retained_root.c').exists():
    M1N1=ROOT.parent.parent/'m1n1_windows'
class Context0BrokerTests(unittest.TestCase):
    def test_full_inventory_and_every_failed_leaf(self):
        sources=['context0_broker','initdata_memory','initdata','firmware_status',
                 'channel_info','channel_memory','regionb','regionb_memory',
                 'render_shared_memory','g13_compute_work','render_template.generated','relocation','regionc',
                 'uat_memory','uat_table','uat','memory']
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'test'
            cmd=[os.environ.get('CC','clang'),'-std=c11','-O1','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-I',str(SHARED/'include'),
                 str(SHARED/'tests/apple_agx_context0_broker_test.c')]
            cmd += [str(SHARED/f'src/apple_agx_{s}.c') for s in sources]
            cmd += [str(M1N1/'src/hv_agx_retained_root.c'),
                    str(M1N1/'src/hv_agx_retained_mmio.c'),'-o',str(exe)]
            subprocess.run(cmd,check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == '__main__':
    unittest.main()
