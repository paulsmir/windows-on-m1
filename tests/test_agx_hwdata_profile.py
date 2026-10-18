import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class HwdataProfileContract(unittest.TestCase):
    def test_exact_inputs_and_integer_materialization(self):
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(ROOT/'tests/agx_hwdata_profile_test.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
