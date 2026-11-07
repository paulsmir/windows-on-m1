#!/usr/bin/env python3
"""Dual-boot contract: every boot clears StartOverride of BOTH boot storage drivers (ANS010-R6:
Windows demotes the unused one, winload honours it -> 0x7B in the other mode); install keeps
AppleAns boot-start and refuses without the devnode/ImagePath; uninstall returns to Start=3."""
from pathlib import Path
src = (Path(__file__).resolve().parents[2] / 'tools/ans-dual-boot.ps1').read_text()
clear = src[src.index('function Clear-Overrides'):src.index('function Status')]
assert "foreach ($n in 'AppleAns', 'stornvme')" in clear and 'StartOverride' in clear
install = src[src.index("'install' {"):src.index("'uninstall' {")]
for needle in ('-AtStartup', 'RepetitionInterval', "UserId 'SYSTEM'", 'Name Start -Value 0',
               'Clear-Overrides', 'devnode missing', 'ImagePath missing', 'stornvme is not boot-start'):
    assert needle in install, needle
uninstall = src[src.index("'uninstall' {"):]
assert 'Unregister-ScheduledTask' in uninstall and 'Name Start -Value 3' in uninstall
print('AppleAns dual-boot contract PASS')
