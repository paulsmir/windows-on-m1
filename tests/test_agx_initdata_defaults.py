import pathlib,struct,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
SHARED=ROOT/'drivers/apple-agx/shared'
class NativeInitdataDefaults(unittest.TestCase):
    def test_real_graph_native_defaults(self):
        modules='initdata_memory initdata firmware_status channel_info channel_memory regionb regionb_memory render_shared_memory render_template.generated regionc uat_memory uat_table uat memory'.split()
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(SHARED/'include'),str(ROOT/'tests/agx_initdata_defaults_test.c'),*[str(SHARED/'src'/('apple_agx_'+m+'.c')) for m in modules],'-o',str(exe)],check=True)
            raw=subprocess.check_output([str(exe)])
        objects=[]
        while raw:
            n=struct.unpack('<I',raw[:4])[0];objects.append(raw[4:4+n]);raw=raw[4+n:]
        script=r'''
import sys,struct,pathlib
sys.path.insert(0,'m1n1_windows/proxyclient')
from m1n1.constructutils import Ver
Ver.set_version_key('V','V13_5');Ver.set_version_key('G','G13')
from m1n1.fw.agx.initdata import InitData_GPUGlobalStatsTA,InitData_GPUGlobalStats3D
for cls in (InitData_GPUGlobalStatsTA,InitData_GPUGlobalStats3D):
 b=cls.build(cls());sys.stdout.buffer.write(struct.pack('<I',len(b))+b)
'''
        raw=subprocess.check_output([str(ROOT/'proxyenv/bin/python'),'-c',script],cwd=ROOT)
        for actual in objects[:2]:
            n=struct.unpack('<I',raw[:4])[0];self.assertEqual(actual,raw[4:4+n]);raw=raw[4+n:]
        expected_tail=bytearray(len(objects[2])-0x224);expected_tail[0x6b38-0x224]=0xff
        self.assertEqual(objects[2][0x224:],expected_tail)
        adt=ROOT/'.local/experiments/EXP476-initdata-inputs/j313-live.adt'
        if adt.exists():
            rc=script[:script.index('for cls in')]+r'''
from m1n1.adt import load_adt
from m1n1.agx.initdata import CHIP_INFO
from m1n1.fw.agx.initdata import InitData_RegionC
sgx=load_adt(pathlib.Path(sys.argv[1]).read_bytes())['/arm-io/sgx']
sys.stdout.buffer.write(InitData_RegionC.build(InitData_RegionC(sgx,CHIP_INFO[0x8103])))
'''
            self.assertEqual(objects[3],subprocess.check_output([str(ROOT/'proxyenv/bin/python'),'-c',rc,str(adt)],cwd=ROOT))
