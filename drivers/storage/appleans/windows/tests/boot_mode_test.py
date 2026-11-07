#!/usr/bin/env python3
"""Boot-mode switch contract (ANS010-R7): native clears AppleAns StartOverride and requires the
package records + persisted devnode; every mode clears stornvme StartOverride (recovery boot driver)."""
from pathlib import Path
import re
src = (Path(__file__).resolve().parents[2] / 'tools/ans-boot-mode.ps1').read_text()
native = src[src.index("if ($Mode -eq 'native')"):src.index('} else {')]
assert "Clear-Override 'AppleAns'" in native and 'Start -Value 0' in native
assert 'records missing' in native and 'devnode missing' in native
tail = src[src.index('} else {'):]
assert 'Start -Value 3' in tail and "Clear-Override 'stornvme'" in tail
assert re.search(r"Clear-Override 'stornvme'\s*\n\s*if \(\(Get-ItemProperty \"\$svc\\stornvme\"\)\.Start -ne 0\)", tail)
print('AppleAns boot-mode switch contract PASS')
