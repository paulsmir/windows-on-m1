"""EXP1094: UMD diagnostics read their environment configuration once.

EXP1092 context-switch samples put ~3.5 % of DWM's composition thread in
RtlQueryEnvironmentVariable under AdmissionUmdDiagnostic: every diagnostic
call (several per submission, including suppressed ddi-* ones) looked up
APPLE_AGX_UMD_REFUSALS_ONLY and APPLE_AGX_UMD_TRACE_FILE. Invariant: the
per-call paths (AdmissionUmdDiagnostic, AdmissionUmdDiagnosticEnabled)
perform no environment lookup; both variables are read only by the
one-time InitOnceExecuteOnce callback.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c'


def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


class TraceConfigOnce(unittest.TestCase):
    def test_hot_paths_do_not_query_the_environment(self):
        text = SRC.read_text()
        for signature in ('VOID AdmissionUmdDiagnostic(', 'BOOL AdmissionUmdDiagnosticEnabled('):
            function = body(text, signature)
            self.assertNotIn('GetEnvironmentVariable', function, signature)
            self.assertIn('AdmissionUmdTraceConfig(', function, signature)
        load = body(text, 'static BOOL CALLBACK AdmissionUmdTraceLoad(')
        for name in ('APPLE_AGX_UMD_REFUSALS_ONLY', 'APPLE_AGX_UMD_TRACE_FILE'):
            self.assertEqual(text.count('L"%s"' % name), 1, name)
            self.assertIn('L"%s"' % name, load)
        config = body(text, 'static PCWSTR AdmissionUmdTraceConfig(')
        self.assertIn('InitOnceExecuteOnce(&AdmissionUmdTraceOnce, AdmissionUmdTraceLoad', config)


if __name__ == '__main__':
    unittest.main()
