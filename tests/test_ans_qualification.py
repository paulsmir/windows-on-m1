"""Exact qualification evidence and pre-hardware refusal; all artifacts synthetic."""
import contextlib
import copy
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from tools.ans_qualification import CANONICAL,PINNED,ROLES,SCHEMA,digest,validate_launch,validate_manifest
ROOT=Path(__file__).resolve().parents[1]

class QualificationTests(unittest.TestCase):
    def setUp(self):
        parent=ROOT/'.local/ans-native';parent.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='qualification-test-',dir=parent)
        self.addCleanup(self.temp.cleanup);self.d=Path(self.temp.name);self.paths={}
        for role in ROLES:
            p=ROOT/CANONICAL[role] if role in CANONICAL else self.d/(role+'.json')
            if role not in CANONICAL:p.write_text(role)
            self.paths[role]=p
        self.paths['m1n1_build'].write_text(json.dumps({'exit_code':0,'frozen_inputs_unchanged':True,
            'build_tag':'2209942','artifacts':{'m1n1.macho':digest(self.paths['m1n1'])}}))
        self.paths['mu_build'].write_text(json.dumps({'builds':[{'profile':'native','exit_code':0,
            'unchanged_source_files':1,'expected_core_gate':'AppleAnsExitBootServicesGateLib',
            'expected_autoboot':'WindowsAutoBootAnsNative','native_source_guard_cache_symbol':True,
            'g2_inert_no_operationregion_or_power_methods':True,'acpi_ffs_hid_counts':{'APPL0002':1,'APPL0003':1},
            'artifacts':[{'path':'fixture.fd','sha256':digest(self.paths['mu'])}]}]}))
        audit={'schema':'apple-ans-winpe-audit/v1','result':'pass','image_sha256':digest(self.paths['winpe']),
               'architecture':'ARM64','boot_index':1,'boot_source':'preloaded-ramdisk','installed_windows_boot':False,
               'indices':[{'index':i,'appleans_packages':[],'appleans_services':[],'appleans_drivers':[]} for i in (1,2)]}
        self.paths['winpe_audit'].write_text(json.dumps(audit))
        self.paths['winpe_collector'].write_text(json.dumps({'schema':'apple-ans-winpe-collector-test/v1',
            'result':'pass','image_sha256':digest(self.paths['winpe']),'hardware_access':False}))
        self.m={'schema':SCHEMA,'experiment':'ANS001','platform':'J313','storage_mode':'ANS_NATIVE',
            'scope':'ans0-unbound-no-driver','read_only':True,'launch':copy.deepcopy(PINNED),'artifacts':{}}
        self.path=self.d/'manifest.json';self.seal()
    def seal(self):
        self.m['artifacts']={r:{'path':str(p.relative_to(ROOT)),'sha256':digest(p)} for r,p in self.paths.items()}
        audit={'schema':'apple-ans-qualification-audit/v1','result':'pass',
               'bindings':{r:self.m['artifacts'][r]['sha256'] for r in ROLES if r!='qualification_audit'}}
        for k in ('offline_gates_passed','mu_ramdisk_only','mu_native_gate_bound','mu_native_autoboot_bound',
                  'agx_power_broker_disabled','no_installed_windows_boot','collector_test_passed'):audit[k]=True
        if self.m['experiment']!='ANS001':
            audit['winpe_appleans_profile_bound']=True;audit['winpe_testsigning']=True
            audit['appleans_package_sha256']='c'*64;audit['appleans_profile']=self.audit_profile
        else:audit['winpe_no_appleans']=True
        self.paths['qualification_audit'].write_text(json.dumps(audit))
        self.m['artifacts']['qualification_audit']['sha256']=digest(self.paths['qualification_audit'])
        self.path.write_text(json.dumps(self.m));self.sha=digest(self.path)
    def call(self,**kw):return validate_launch(self.path,self.sha,ROOT,environment={},**kw)
    def test_positive_dryrun_and_explicit_confirmation(self):
        self.assertEqual(self.call()[0]['experiment'],'ANS001')
        self.call(execute=True,confirmation='ANS001')
        with self.assertRaisesRegex(ValueError,'confirm-experiment'):self.call(execute=True)
    def test_every_artifact_tamper_or_missing_rejected(self):
        for role in ROLES:
            bad=copy.deepcopy(self.m);bad['artifacts'][role]['sha256']='0'*64
            self.path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):validate_manifest(self.path,digest(self.path),ROOT)
        self.path.write_text(json.dumps(self.m));self.sha=digest(self.path)
        self.paths['m1n1'].unlink()
        with self.assertRaises(ValueError):self.call()
    def test_output_aliases_cannot_truncate_sealed_input(self):
        target=self.paths['winpe'];before=target.read_bytes()
        symlink=self.d/'symlink-output';symlink.symlink_to(target)
        hardlink=self.d/'hardlink-output';os.link(target,hardlink)
        for output in (target,symlink,hardlink,self.path):
            with self.assertRaisesRegex(ValueError,'output aliases'):self.call(contract_output=output)
        self.assertEqual(target.read_bytes(),before)
        from tools.ans_qualification import reject_output_aliases
        with tempfile.TemporaryDirectory(dir=target.parent) as root:  # hardlink needs one device
            log=Path(root)/'hv.log';os.link(target,log)
            with self.assertRaisesRegex(ValueError,'output aliases'):
                reject_output_aliases(root,self.paths,self.path)
    def test_actual_runner_alias_refused_before_any_hardware(self):
        before=self.paths['winpe'].read_bytes()
        argv=self.argv()+['--device','synthetic-proxy','--qualified-vuart','synthetic-vuart',
                          '--confirm-experiment','ANS001','--contract-output',str(self.paths['winpe'])]
        with patch.dict(os.environ,{},clear=True),patch.object(sys,'argv',argv),patch('subprocess.run',side_effect=AssertionError('hardware before alias validation')):
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as stop:
                runpy.run_path(str(ROOT/'run_uefi.py'),run_name='__main__')
        self.assertNotEqual(stop.exception.code,0)
        self.assertEqual(self.paths['winpe'].read_bytes(),before)
    def test_bad_seal_profile_and_override(self):
        with self.assertRaises(ValueError):validate_manifest(self.path,'0'*64,ROOT)
        for args in [{'overrides':['phys_base']},{'no_pin':True},{'wdt_cpu':0},{'agx_power_broker':True},
                     {'display':'virtual'},{'debug':'off'},{'low_mem':False},{'firmware':ROOT/'wrong.fd'}]:
            with self.assertRaises(ValueError):self.call(**args)
        for key,value in [('experiment','ANS002'),('scope','installed-windows'),('read_only',False)]:
            bad=copy.deepcopy(self.m);bad[key]=value;self.path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):validate_manifest(self.path,digest(self.path),ROOT)
    def test_inherited_unsafe_environment(self):
        for name in ('WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT','WOM1_ALLOW_LEGACY_HV_INIT_ZERO','WOM1_AGX_G2_POWER_BROKER'):
            with self.assertRaises(ValueError):validate_launch(self.path,self.sha,ROOT,environment={name:'1'})
    def test_winpe_packages_and_unproven_collector_rejected(self):
        p=self.paths['winpe_audit'];a=json.loads(p.read_text());a['indices'][1]['appleans_packages']=['oem1.inf'];p.write_text(json.dumps(a));self.seal()
        with self.assertRaisesRegex(ValueError,'AppleAns'):self.call()
        a['indices'][1]['appleans_packages']=[];p.write_text(json.dumps(a));self.seal()
        p=self.paths['winpe_collector'];a=json.loads(p.read_text());a['result']='pending';p.write_text(json.dumps(a));self.seal()
        with self.assertRaisesRegex(ValueError,'collector'):self.call()
    def make_ans002(self,profile='native-read-only-stop1',second_index_pkgs=(),testsigning=True,confirm_flag=True,
                    experiment='ANS002',scope='ans1-claim-stop',audit_profile=None):
        self.m['experiment']=experiment;self.m['scope']=scope;self.audit_profile=audit_profile or profile
        p=self.paths['winpe_audit'];a=json.loads(p.read_text())
        a['appleans_profile']=profile;a['appleans_package_sha256']='c'*64;a['testsigning']=testsigning
        a['indices'][0]['appleans_packages']=['oem0.inf'];a['indices'][1]['appleans_packages']=list(second_index_pkgs)
        p.write_text(json.dumps(a));self.ans002_flags=confirm_flag;self.seal()
    def test_ans002_claim_stop_profile_admitted_only_with_exact_package(self):
        self.make_ans002()
        self.assertEqual(self.call()[0]['experiment'],'ANS002')
        self.call(execute=True,confirmation='ANS002')
        with self.assertRaisesRegex(ValueError,'confirm'):self.call(execute=True,confirmation='ANS001')
    def test_ans002_rejects_dma_capable_profiles_and_extra_packages(self):
        for kw in ({'profile':'native-read-only-stop2'},{'profile':'native-read-only-stop0'},
                   {'second_index_pkgs':('oem1.inf',)},{'testsigning':False}):
            self.setUp();self.make_ans002(**kw)
            with self.assertRaises(ValueError):self.call()
    def test_ladder_profiles_bind_exact_stop_level(self):
        for exp,scope,prof in (('ANS003','ans2-admin-ready-stop','native-read-only-stop2'),
                               ('ANS004','ans3-identify-stop','native-read-only-stop3'),
                               ('ANS005','ans4-read-only-full','native-read-only-stop0'),
                               ('ANS006','ans5-read-only-volume','native-read-only-stop0')):
            self.setUp();self.make_ans002(profile=prof,experiment=exp,scope=scope)
            self.assertEqual(self.call(execute=True,confirmation=exp)[0]['experiment'],exp)
            self.setUp();self.make_ans002(profile='native-read-only-stop1',experiment=exp,scope=scope)
            with self.assertRaises(ValueError):self.call()
            self.setUp();self.make_ans002(profile=prof,experiment=exp,scope=scope,audit_profile='native-read-only-stop1')
            with self.assertRaises(ValueError):self.call()
    def test_ans001_still_rejects_any_package(self):
        p=self.paths['winpe_audit'];a=json.loads(p.read_text());a['indices'][0]['appleans_packages']=['oem0.inf']
        a['appleans_profile']='native-read-only-stop1';p.write_text(json.dumps(a));self.seal()
        with self.assertRaisesRegex(ValueError,'AppleAns'):self.call()
    def argv(self):return ['run_uefi.py','--storage-mode','ANS_NATIVE','--qualification-manifest',str(self.path),
        '--qualification-sha256',self.sha,'--display-mode','physical','--debug-mode','uart','--low-mem']
    def test_failed_build_and_recovery_firmware_receipt_rejected(self):
        p=self.paths['mu_build'];a=json.loads(p.read_text());a['builds'][0]['profile']='recovery';p.write_text(json.dumps(a));self.seal()
        with self.assertRaisesRegex(ValueError,'native Mu'):self.call()
        a['builds'][0]['profile']='native';a['builds'][0]['exit_code']=1;p.write_text(json.dumps(a));self.seal()
        with self.assertRaisesRegex(ValueError,'Mu native'):self.call()
    def test_actual_runner_dryrun_never_imports_uart_or_chainloads(self):
        before=set(sys.modules)
        with patch.dict(os.environ,{},clear=True),patch.object(sys,'argv',self.argv()+['--dry-run']),patch('subprocess.run',side_effect=AssertionError('hardware subprocess')):
            with contextlib.redirect_stdout(io.StringIO()),self.assertRaises(SystemExit) as stop:runpy.run_path(str(ROOT/'run_uefi.py'),run_name='__main__')
        self.assertEqual(stop.exception.code,0);self.assertNotIn('m1n1.proxy',set(sys.modules)-before)
    def test_execution_without_confirmation_stops_before_hardware(self):
        with patch.dict(os.environ,{},clear=True),patch.object(sys,'argv',self.argv()),patch('subprocess.run',side_effect=AssertionError('hardware subprocess')):
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as stop:runpy.run_path(str(ROOT/'run_uefi.py'),run_name='__main__')
        self.assertNotEqual(stop.exception.code,0)
    def test_exec_checks_conflict_then_exact_chainload_before_uart(self):
        class StopBeforeHardware(Exception): pass
        events=[]
        def chainload(argv, **kwargs):
            events.append('chainload')
            self.assertTrue(argv[1].endswith('proxyclient/tools/chainload.py'))
            self.assertEqual(argv[2],str(self.paths['m1n1']))
            raise StopBeforeHardware()
        with patch.dict(os.environ,{},clear=True),patch.object(sys,'argv',self.argv()+['--device','synthetic-port','--qualified-vuart','synthetic-vuart','--confirm-experiment','ANS001']),patch('fcntl.flock'),patch('tools.ans_qualification.reject_running_launcher',side_effect=lambda:events.append('conflict')),patch('subprocess.run',side_effect=chainload):
            with contextlib.redirect_stdout(io.StringIO()),self.assertRaises(StopBeforeHardware):
                runpy.run_path(str(ROOT/'run_uefi.py'),run_name='__main__')
        self.assertEqual(events,['conflict','chainload'])
    def test_native_uart_reader_starts_after_chainload_before_guest_uart(self):
        class StopBeforeGuest(Exception): pass
        events=[];real_open=Path.open
        def opened(path,*args,**kwargs):
            if path.name=='guest-uart-reader.log':return io.BytesIO()
            return real_open(path,*args,**kwargs)
        def reader(argv,**kwargs):
            events.append('reader')
            self.assertTrue(argv[2].endswith('extra/uart-reader.py'))
            self.assertEqual(argv[3],'synthetic-vuart')
            raise StopBeforeGuest()
        argv=self.argv()+['--device','synthetic-proxy','--qualified-vuart','synthetic-vuart','--confirm-experiment','ANS001']
        with patch.dict(os.environ,{},clear=True),patch.object(sys,'argv',argv),patch('fcntl.flock'),patch('builtins.open',side_effect=lambda *a,**k:tempfile.TemporaryFile()),patch('tools.ans_qualification.reject_running_launcher'),patch('subprocess.run',side_effect=lambda *a,**k:events.append('chainload')),patch('subprocess.Popen',side_effect=reader):
            with contextlib.redirect_stdout(io.StringIO()),self.assertRaises(StopBeforeGuest):
                runpy.run_path(str(ROOT/'run_uefi.py'),run_name='__main__')
        self.assertEqual(events,['chainload','reader'])
    def test_actual_shell_valid_dryrun_no_endpoint_discovery(self):
        p=subprocess.run(['sh',str(ROOT/'scripts/run-assisted.sh'),'--storage-mode','ANS_NATIVE','--chainload',
                          '--firmware',str(self.paths['mu']),'--m1n1',str(self.paths['m1n1']),
                          '--ramdisk',str(self.paths['winpe']),'--display','physical','--debug','uart',
                          '--qualification-manifest',str(self.path),'--qualification-sha256',self.sha,'--dry-run'],
                         cwd=ROOT,capture_output=True,text=True,env={'PATH':os.environ['PATH']})
        self.assertEqual(p.returncode,0,p.stderr);self.assertIn('proxy: <proxy-device>',p.stdout)
    def test_failed_mu_guard_receipt_not_admitted(self):
        p=self.paths['qualification_audit'];a=json.loads(p.read_text());a['mu_ramdisk_only']=False;p.write_text(json.dumps(a))
        self.m['artifacts']['qualification_audit']['sha256']=digest(p);self.path.write_text(json.dumps(self.m));self.sha=digest(self.path)
        with self.assertRaisesRegex(ValueError,'mu_ramdisk_only'):self.call()
    def test_actual_shell_invalid_native_stops_before_discovery(self):
        p=subprocess.run(['sh',str(ROOT/'scripts/run-assisted.sh'),'--storage-mode','ANS_NATIVE','--chainload',
                          '--firmware',str(self.paths['mu']),'--m1n1',str(self.paths['m1n1']),
                          '--ramdisk',str(self.paths['winpe']),'--display','physical','--debug','uart',
                          '--qualification-manifest',str(self.path),'--qualification-sha256',self.sha],
                         cwd=ROOT,capture_output=True,text=True,env={'PATH':os.environ['PATH']})
        self.assertNotEqual(p.returncode,0);self.assertRegex(p.stderr,'confirm-experiment|launcher is active|proxy lock')
        self.assertNotIn('Connect the Air',p.stderr)

if __name__=='__main__':unittest.main()
