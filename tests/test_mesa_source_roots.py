# Original deterministic builder configuration regression. SPDX-License-Identifier: MIT
import ast, pathlib, tempfile, types, unittest
BASE=pathlib.Path(__file__).resolve().parents[1]/'drivers/apple-agx/mesa/scripts'
NAMES=('build-full-asahi.py','build-native-asahi-state.py','build-asahi-runtime-closure.py')
def source_assignment(tree,name):
    candidates=[n for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets) and any(isinstance(v,ast.Attribute) and v.attr=='mesa_source' for v in ast.walk(n.value))]
    if len(candidates)!=1: raise AssertionError('one authoritative source root required')
    return candidates[0]
class SourceRootRegression(unittest.TestCase):
    def test_actual_default_and_override_bindings_select_distinct_primary_bytes(self):
        # Execute only actual source-selection assignments, never builder top-level code.
        for filename in NAMES:
            with self.subTest(builder=filename),tempfile.TemporaryDirectory() as d:
                tree=ast.parse((BASE/filename).read_text());name='mesa' if filename=='build-native-asahi-state.py' else 'MESA'
                assignment=source_assignment(tree,name)
                default=pathlib.Path(d)/'old';chosen=pathlib.Path(d)/'projected'
                for root,data in [(default,b'pristine'),(chosen,b'projected')]:
                    (root/'src/compiler').mkdir(parents=True);(root/'src/compiler/shader_info.h').write_bytes(data)
                # For native builder its literal default is in the actual expression;
                # replace only that fixture-specific default literal with owned fixture root.
                if name=='mesa':
                    for n in ast.walk(assignment):
                        if isinstance(n,ast.Constant) and isinstance(n.value,str) and 'AD04-d3d10-frontend-build' in n.value:n.value=str(default)
                code=compile(ast.fix_missing_locations(ast.Module(body=[assignment],type_ignores=[])),filename,'exec')
                for override,expected in [(None,b'pristine'),(chosen,b'projected')]:
                    options=types.SimpleNamespace(mesa_source=override);env={'Path':pathlib.Path,'BUILDER':pathlib.Path('/builder-root'),'ARGS':options,'args':options,'MESA':default,'mesa':default}
                    exec(code,env)
                    self.assertEqual((env[name]/'src/compiler/shader_info.h').read_bytes(),expected)
    def test_runtime_rejects_mixed_preparation_and_compiler_evidence(self):
        # Execute the actual receipt-identity guard from runtime, with real owned JSON files.
        import json,hashlib
        tree=ast.parse((BASE/'build-asahi-runtime-closure.py').read_text())
        candidates=[n for n in ast.walk(tree) if isinstance(n,ast.If) and any(isinstance(v,ast.Name) and v.id=='source_identity' for v in ast.walk(n)) and any(isinstance(v,ast.For) for v in n.body)]
        self.assertEqual(len(candidates),1)
        code=compile(ast.fix_missing_locations(ast.Module(body=[candidates[0]],type_ignores=[])),'actual-runtime-identity','exec')
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);mesa=root/'mesa';native=root/'native';evidence=root/'evidence';out=root/'out';build=root/'build'
            for p in [mesa/'src/compiler',native,evidence,out,build]:p.mkdir(parents=True)
            (mesa/'src/compiler/shader_info.h').write_bytes(b'one selected header')
            identity={'mesa_source':str(mesa.resolve()),'mesa_build':str(build.resolve()),'shader_info_sha256':hashlib.sha256(b'one selected header').hexdigest()}
            for p in [build,native,evidence]:(p/'source-root.json').write_text(json.dumps(identity))
            env={'Path':pathlib.Path,'args':types.SimpleNamespace(mesa_source=mesa),'MESA':mesa,'native':native,'evidence':evidence,'out':out,'build':build,'json':json,'sha256':lambda p:hashlib.sha256(p.read_bytes()).hexdigest()}
            exec(code,env);self.assertEqual(json.loads((out/'source-root.json').read_text()),identity)
            wrong=dict(identity);wrong['shader_info_sha256']='old header'
            for changed in [build,native,evidence]:
                with self.subTest(mixed_receipt=changed.name):
                    receipt=changed/'source-root.json'
                    receipt.write_text(json.dumps(wrong))
                    with self.assertRaisesRegex(RuntimeError,'identity mismatch'):exec(code,env)
                    receipt.write_text(json.dumps(identity))
    def test_compiler_evidence_refuses_unqualified_core_libraries(self):
        import json,hashlib
        tree=ast.parse((BASE/'build-full-asahi.py').read_text())
        blocks=[n for n in tree.body if isinstance(n,ast.If) and any(isinstance(v,ast.Name) and v.id=='source_identity' for v in ast.walk(n))]
        self.assertEqual(len(blocks),1)
        code=compile(ast.fix_missing_locations(ast.Module(body=blocks,type_ignores=[])),'actual-compiler-core-identity','exec')
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);mesa=root/'mesa';build=root/'core';out=root/'evidence'
            for q in [mesa/'src/compiler',build,out]:q.mkdir(parents=True)
            (mesa/'src/compiler/shader_info.h').write_bytes(b'projected header')
            identity={'mesa_source':str(mesa.resolve()),'mesa_build':str(build.resolve()),'shader_info_sha256':hashlib.sha256(b'projected header').hexdigest()}
            env={'Path':pathlib.Path,'ARGS':types.SimpleNamespace(mesa_source=mesa),'MESA':mesa,'BUILD':build,'OUT':out,'json':json,'hashlib':hashlib}
            with self.assertRaises(FileNotFoundError):exec(code,env)
            receipt=build/'source-root.json';receipt.write_text(json.dumps(dict(identity,shader_info_sha256='pristine')))
            with self.assertRaisesRegex(RuntimeError,'identity mismatch'):exec(code,env)
            receipt.write_text(json.dumps(identity));exec(code,env)
            self.assertEqual(json.loads((out/'source-root.json').read_text()),identity)
if __name__=='__main__':unittest.main()
