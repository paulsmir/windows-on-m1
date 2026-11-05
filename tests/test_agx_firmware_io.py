import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class FirmwareIoContract(unittest.TestCase):
    def test_manifest_validation_and_native_descriptors(self):
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(ROOT/'tests/agx_firmware_io_test.c'),'-o',str(exe)],check=True)
            actual=subprocess.check_output([str(exe),'emit'])
            script=r'''
import sys,struct
sys.path.insert(0,'m1n1_windows/proxyclient')
from m1n1.agx.initdata import build_iomappings
from m1n1.malloc import Heap
class Uat:
 def iomap_at(self,*args,**kwargs): pass
class Agx:
 io_allocator=Heap(0xffffffa068000000,0xffffffa070000000,block=0x4000)
 uat=Uat()
records=build_iomappings(Agx(),0x8103)
sys.stdout.buffer.write(b''.join(struct.pack('<QQIIQ',x.phys_addr,x.virt_addr,x.size,x.range_size,x.readwrite) for x in records))
'''
            expected=subprocess.check_output([str(ROOT/'proxyenv/bin/python'),'-c',script],cwd=ROOT)
            self.assertEqual(actual,expected)
