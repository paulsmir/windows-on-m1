#!/usr/bin/env python3
"""Reject a recovery script whose external commands are absent from its WIM.

This intentionally accepts only the small batch grammar used by this recovery
script. Unknown commands become dependencies, never silently assumed built-ins.
"""
import argparse
from pathlib import Path, PureWindowsPath
import subprocess

BUILTINS = {'echo', 'setlocal', 'set', 'if', 'md', 'copy', 'type', 'pause', 'exit', 'goto', 'rem', 'call'}

def dependencies(script):
    result = {'cmd.exe', 'wpeinit.exe'}
    for line in script.splitlines():
        line = line.strip().lstrip('@')
        if not line or line.startswith(':'):
            continue
        if line.startswith('>'):
            # Our manifest writes are redirection followed by built-in echo.
            if ' echo ' not in line.lower():
                raise ValueError('Unsupported redirected command: ' + line)
            continue
        command = line.split()[0].strip('"')
        if command.lower() in BUILTINS:
            continue
        name = PureWindowsPath(command).name.lower()
        result.add(name if name.endswith('.exe') else name + '.exe')
    return result

def main():
    p = argparse.ArgumentParser()
    p.add_argument('wim')
    p.add_argument('script')
    p.add_argument('--wimlib', default='wimlib-imagex')
    args = p.parse_args()
    required = dependencies(Path(args.script).read_text())
    failed = False
    for index in (1, 2):
        listing = subprocess.check_output([args.wimlib, 'dir', args.wim, str(index), '--path=/Windows/System32'], text=True)
        available = {line.strip().lower() for line in listing.splitlines()}
        missing = sorted(name for name in required if '/windows/system32/' + name not in available)
        print(f'INDEX={index} REQUIRED={sorted(required)} MISSING={missing}')
        failed |= bool(missing)
    return 1 if failed else 0

if __name__ == '__main__':
    raise SystemExit(main())
