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
