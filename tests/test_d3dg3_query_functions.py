"""D3D-G3 (EXP1283): the Windows Asahi context keeps Mesa's query functions.

Bug caught: the native projection deleted agx_init_query_functions(pctx) from
agx_create_context, so pipe->create_query (and begin/end/get_query_result,
render_condition) stayed NULL and ctx->active_queries stayed false. EXP1283's
first D3D CreateQuery(OCCLUSION) called a NULL create_query and the process
died; a no-op set_active_query_state had been added only because u_blitter
called the missing one. A projection that exposes occlusion queries (the
d3d10umd overlay creates pipe queries) must keep the upstream initialisation
and the upstream set_active_query_state.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"
FRONTEND = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def removed_context_lines(text):
    """The lines the projection deletes from agx_create_context."""
    start = text.index("a,b=function(s,'agx_create_context')")
    section = text[start:text.index("s=s[:a]+part+s[b:]", start)]
    removed = []
    for match in re.finditer(r"for line in \((.*?)\):\s*\n\s*part=part\.replace\(line,''\)",
                             section, re.S):
        removed += re.findall(r"'([^']*)'", match.group(1))
    return removed


class QueryFunctionsKept(unittest.TestCase):
    def test_context_keeps_mesa_query_functions(self):
        frontend = FRONTEND.read_text()
        self.assertIn("pDevice->pipe->create_query(", frontend)
        lifecycle = LIFECYCLE.read_text()
        removed = removed_context_lines(lifecycle)
        self.assertTrue(removed, "the agx_create_context removal list moved")
        self.assertFalse([line for line in removed if "agx_init_query_functions" in line])

    def test_blitter_uses_the_upstream_active_query_state(self):
        lifecycle = LIFECYCLE.read_text()
        self.assertNotIn("set_active_query_state =", lifecycle)


if __name__ == "__main__":
    unittest.main()
