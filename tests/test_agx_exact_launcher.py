"""Real invariant: path aliases must select one exact direct launcher, never a shell."""
import errno
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('exact_launcher', Path(__file__).resolve().parents[1] / 'scripts/agx-select-exact-launcher.py')
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


class ExactLauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.work = self.root / 'real worktree'
        self.work.mkdir()
        self.launcher = self.work / 'run_uefi.py'
        self.launcher.write_text('# fixture\n')
        self.exp = self.work / 'experiment'
        self.exp.mkdir()
        self.contract = self.exp / 'contract.bin'
        self.contract.write_bytes(b'contract')
        self.alias = self.root / 'alias worktree'
        self.alias.symlink_to(self.work, target_is_directory=True)
        self.argv = ['/usr/bin/python3', '-u', str(self.alias / 'run_uefi.py'), 'firmware.fd', '--contract-output', str(self.alias / 'experiment/contract.bin')]

    def owner(self, argv=None):
        return m.is_owner('/usr/bin/python3', self.argv if argv is None else argv, self.launcher, self.contract)

    def select(self, snapshots):
        return m.select_owner(list(snapshots), snapshots.__getitem__, self.launcher, self.contract)

    def test_alias_identity_and_spaces(self):
        # The previous lexical pgrep expression cannot match these equivalent paths.
        self.assertNotIn(str(self.exp / 'contract.bin'), ' '.join(self.argv))
        self.assertTrue(self.owner())
        self.assertEqual(self.select({42: ('/usr/bin/python3', self.argv)}), 42)

    def test_ans_production_launcher_is_owner(self):
        # EXP2255 recovery: "unrecognized arguments: --storage-mode ANS_NATIVE --ans-direct --ans-production-fd-sha256"
        argv = self.argv[:4] + ['--storage-mode', 'ANS_NATIVE', '--ans-direct', '--ans-production-fd-sha256', 'ab' * 32] + self.argv[4:]
        self.assertTrue(self.owner(argv))
        self.assertEqual(self.select({7: ('/usr/bin/python3', argv)}), 7)

    def test_option_equals_and_other_contract(self):
        self.assertTrue(self.owner(self.argv[:-2] + ['--contract-output=' + self.argv[-1]]))
        other = self.exp / 'other.bin'
        other.write_bytes(b'contract')
        self.assertFalse(self.owner(self.argv[:-1] + [str(other)]))

    def test_no_owner_and_multiple_owners(self):
        with self.assertRaises(m.SelectionError):
            self.select({})
        with self.assertRaises(m.SelectionError):
            self.select({1: ('python3', self.argv), 2: ('python3', self.argv)})

    def test_shell_and_python_code_are_not_launchers(self):
        self.assertFalse(m.is_owner('/bin/sh', ['sh', '-c', ' '.join(self.argv)], self.launcher, self.contract))
        self.assertFalse(self.owner(['python3', '-c', 'print(1)', str(self.launcher)] + self.argv[-2:]))
        self.assertFalse(self.owner(['python3', '-m', 'run_uefi.py'] + self.argv[-2:]))

    def test_other_worktree_is_not_owner(self):
        other = self.root / 'other'
        other.mkdir()
        script = other / 'run_uefi.py'
        script.write_text('# another file')
        self.assertFalse(self.owner(self.argv[:2] + [str(script)] + self.argv[3:]))

    def test_malformed_options_and_relative_paths_refused(self):
        bad = [self.argv + ['--unknown'], self.argv + ['--dry-run'],
               self.argv[:-2] + ['--'] + self.argv[-2:],
               self.argv[:-2] + ['--device'] + self.argv[-2:],
               self.argv + self.argv[-2:], self.argv[:-1], self.argv[:-2],
               self.argv[:-2] + ['--contract-out', self.argv[-1]],
               self.argv[:-1] + ['relative/contract.bin'],
               self.argv[:2] + ['run_uefi.py'] + self.argv[3:],
               self.argv[:1] + ['-X', 'dev'] + self.argv[1:]]
        for argv in bad:
            with self.subTest(argv=argv), self.assertRaises(m.SelectionError):
                self.owner(argv)

    def test_missing_file_and_symlink_to_wrong_file_refused(self):
        with self.assertRaises(m.SelectionError):
            self.owner(self.argv[:-1] + [str(self.exp / 'absent')])
        self.alias.unlink()
        self.alias.symlink_to(self.root)
        with self.assertRaises(m.SelectionError):
            self.owner()

    def test_read_errors_and_changed_identity_refused(self):
        for error in (errno.EPERM, errno.EINVAL):
            def reader(pid):
                raise OSError(error, 'fixture')
            with self.assertRaises(m.SelectionError):
                m.select_owner([42], reader, self.launcher, self.contract)
        calls = []
        def changing(pid):
            calls.append(pid)
            return ('python3', self.argv if len(calls) == 1 else self.argv + ['new'])
        with self.assertRaises(m.SelectionError):
            m.select_owner([42], changing, self.launcher, self.contract)

    def test_procargs_preserves_empty_and_space_arguments_ignores_environment(self):
        argv = ['python3', '-u', '/space path/run_uefi.py', '', 'x y']
        blob = struct.pack('@i', len(argv)) + b'/usr/bin/python3\0\0\0' + b'\0'.join(x.encode() for x in argv) + b'\0SECRET=not-an-argument\0'
        self.assertEqual(m.decode_procargs(blob), ('/usr/bin/python3', argv))
        for bad in (b'', struct.pack('@i', 0) + b'x\0', blob[:20], struct.pack('@i', 3) + b'p\0p\0'):
            with self.subTest(bad=bad), self.assertRaises(m.SelectionError):
                m.decode_procargs(bad)


if __name__ == '__main__':
    unittest.main()


@unittest.skipUnless(__import__('sys').platform.startswith('linux'), 'Linux /proc reader')
class LinuxProcReaderTests(unittest.TestCase):
    """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? On the WSL host (2026-10-25) the selector only knew
    Darwin KERN_PROCARGS2, so every recovery with a live run_uefi.py owner would refuse. The /proc reader
    must return the exact argv (spaces and empty arguments intact) of a live direct launcher, select it, and
    report a vanished process as ESRCH."""

    def test_live_direct_launcher_is_selected_from_proc(self):
        import subprocess, sys, time
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp) / 'real worktree'; work.mkdir()
            launcher = work / 'run_uefi.py'; launcher.write_text('import time\ntime.sleep(30)\n')
            contract = work / 'contract.bin'; contract.write_bytes(b'contract')
            argv = [sys.executable, '-u', str(launcher), 'fw with space.fd', '', '--contract-output', str(contract)]
            p = subprocess.Popen(argv)
            self.addCleanup(p.wait); self.addCleanup(p.kill)
            for _ in range(50):
                if Path(f'/proc/{p.pid}/cmdline').read_bytes():
                    break
                time.sleep(0.05)
            executable, seen = m.read_procargs(p.pid)
            self.assertEqual(seen, argv)
            self.assertTrue(m.python_name(executable))
            # the empty positional is a fixture-only argument; the owner grammar takes one payload
            owner_argv = argv[:4] + argv[5:]
            q = subprocess.Popen(owner_argv)
            self.addCleanup(q.wait); self.addCleanup(q.kill)
            for _ in range(50):
                if Path(f'/proc/{q.pid}/cmdline').read_bytes():
                    break
                time.sleep(0.05)
            self.assertEqual(m.select_owner([q.pid], m.read_procargs, launcher, contract), q.pid)

    def test_vanished_process_is_esrch(self):
        import subprocess, sys
        p = subprocess.Popen([sys.executable, '-c', 'pass']); p.wait()
        with self.assertRaises(OSError) as caught:
            m.read_procargs(p.pid)
        self.assertEqual(caught.exception.errno, errno.ESRCH)
