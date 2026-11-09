#!/usr/bin/env python3
"""SRB_FUNCTION_SHUTDOWN must not end I/O.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The driver stopped the controller on the first shutdown SRB and failed later requests
with SRB_STATUS_NO_DEVICE. Paging I/O during shutdown then failed with
STATUS_NO_SUCH_DEVICE: bugcheck 0x1A (p2 C000000E), 0xEF or 0x154 at each restart after
a session that paged. Microsoft: a miniport can get several shutdown requests before
the system is actually shut down.
"""
import re
from pathlib import Path

SRC = (Path(__file__).resolve().parents[1] / 'native.c').read_text()
start = SRC.index('SRB_FUNCTION_SHUTDOWN')
assert not re.search(r'if\s*\(\s*shutdown\s*\)\s*a->ShuttingDown\s*=\s*TRUE', SRC), 'shutdown must not stop admissions'
assert 'slot->Shutdown=FALSE' in SRC, 'a shutdown SRB completes as an ordinary flush, without the stop DPC'
assert 'if (shutdown) attribute=ANS_ORDER_ORDERED' in SRC, 'shutdown stays an ordered flush'
print('PASS shutdown_serves_io')
