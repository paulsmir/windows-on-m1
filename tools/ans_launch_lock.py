"""Shared assisted bootstrap ownership. No USB operations and no process killing."""
import fcntl
import os
from pathlib import Path
import shlex
import subprocess
import sys

FD_ENV = 'ANS_ASSISTED_LOCK_FD'
OWNER_ENV = 'ANS_ASSISTED_LOCK_OWNER_PID'
NAMES = {'run-assisted.sh', 'chainload.py', 'run_uefi.py'}

def lock_path(root):
    return Path(root).resolve()/'.local/assisted-launch.lock'

def inherited_fd(root):
    value=os.environ.get(FD_ENV)
    if value is None:return None
    try:
        fd=int(value); owner=int(os.environ[OWNER_ENV]); a=os.fstat(fd); b=lock_path(root).stat()
        if owner <= 0:raise ValueError()
        if fd<3 or (a.st_dev,a.st_ino)!=(b.st_dev,b.st_ino):raise ValueError()
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        return fd
    except (OSError,ValueError,KeyError):raise ValueError('invalid inherited assisted ownership descriptor')

def acquire(root):
    fd=inherited_fd(root)
    if fd is not None:return fd
    p=lock_path(root);p.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(p,os.O_RDWR|os.O_CREAT,0o600)
    try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError:
        os.close(fd);raise ValueError('another assisted bootstrap/guest owns the proxy lock')
    os.set_inheritable(fd,True)
    os.environ[FD_ENV]=str(fd)
    os.environ[OWNER_ENV]=str(os.getpid())
    return fd

def _same_file(path, target):
    """argv paths may name the root through a symlink (/Users/... -> /Volumes/...)."""
    try:return os.path.samefile(path,target)
    except OSError:return False

def reject_legacy(root):
    p=subprocess.run(['ps','-axo','pid=,ppid=,args='],capture_output=True,text=True)
    if p.returncode:raise ValueError('cannot establish launcher exclusivity')
    rows={}
    for line in p.stdout.splitlines():
        parts=line.strip().split(None,2)
        if len(parts)==3 and parts[0].isdigit() and parts[1].isdigit():
            rows[int(parts[0])]=(int(parts[1]),parts[2])
    ancestors=set();pid=os.getpid()
    while pid in rows and pid not in ancestors:
        ancestors.add(pid);pid=rows[pid][0]
    own_script=str(Path(root).resolve()/'scripts/run-assisted.sh')
    inherited=inherited_fd(root) is not None
    owner=int(os.environ[OWNER_ENV]) if inherited else None
    for pid,(_,command) in rows.items():
        if pid==os.getpid():continue
        try:words=shlex.split(command)
        except ValueError:continue
        launch=[]
        if words and Path(words[0]).name in NAMES:
            launch=[words[0]]
        elif words and (Path(words[0]).name.lower().startswith('python') or Path(words[0]).name in ('sh','bash','zsh','dash')):
            # Only the interpreter's actual script operand counts. Do not parse
            # embedded -c code or unrelated arguments as active launchers.
            for word in words[1:]:
                if word in ('-c','-m') or (word.startswith('-') and 'c' in word):break
                if word.startswith('-'):continue
                if Path(word).name in NAMES:launch=[word]
                break
        if not launch:continue
        if '--dry-run' in words and all(Path(w).name in {'run-assisted.sh','run_uefi.py'} for w in launch):continue
        # Only our guarded shell/wrapper ancestry is legitimate. An ancestral
        # chainload or original-tree bootstrap is not silently exempted.
        if pid in ancestors and pid == owner and inherited and all(_same_file(w,own_script) for w in launch):continue
        raise ValueError(f'another legacy assisted/bootstrap launcher is active (pid {pid})')

def main():
    check=sys.argv[1:2]==['--check']
    args=sys.argv[2:] if check else sys.argv[1:]
    root=args[0]
    if check:
        if inherited_fd(root) is None:raise ValueError('missing assisted ownership descriptor')
        reject_legacy(root);return
    acquire(root);reject_legacy(root)
    os.execvpe('/bin/sh',['sh',*args[1:]],os.environ)

if __name__=='__main__':
    try:main()
    except ValueError as error:print(str(error),file=sys.stderr);sys.exit(2)
