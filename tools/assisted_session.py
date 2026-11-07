"""Owned UART-reader lifetime: stop/reap our child before releasing bootstrap lock."""
import atexit
import os
import signal
import subprocess

class AssistedSession:
    def __init__(self, lock_fd, timeout=2):
        self.lock_fd=lock_fd
        self.timeout=timeout
        self.reader=None
        self.reader_log=None
        atexit.register(self.close)

    def start_reader(self, command, log_path, cwd):
        if self.reader is not None or self.lock_fd is None:
            raise RuntimeError('reader/session already used')
        self.reader_log=open(log_path,'wb')
        env=dict(os.environ)
        env['ANS_UART_OWNER_PID']=str(os.getpid())
        try:
            self.reader=subprocess.Popen(command,stdin=subprocess.DEVNULL,
                stdout=self.reader_log,stderr=subprocess.STDOUT,start_new_session=True,
                cwd=cwd,env=env,pass_fds=(self.lock_fd,))
        except BaseException:
            self.close()
            raise
        return self.reader

    def close(self):
        if self.lock_fd is None:return True
        reader=self.reader
        if reader is not None and reader.poll() is None:
            # SIGINT unwinds uart-reader.py's with blocks, flushing its logs.
            # Escalate only this exact Popen child; never signal a process group
            # or a PID discovered by name. It retains the guard until reaped.
            for sig in (signal.SIGINT,signal.SIGTERM,signal.SIGKILL):
                if reader.poll() is not None:break
                reader.send_signal(sig)
                try:reader.wait(timeout=self.timeout)
                except subprocess.TimeoutExpired:continue
            if reader.poll() is None:
                # Cannot prove the endpoint is closed. Keep the descriptor; the
                # child also inherits it, so another launch cannot bypass us.
                return False
        if reader is not None:reader.wait()
        try:
            if self.reader_log is not None:
                try:
                    self.reader_log.flush()
                    os.fsync(self.reader_log.fileno())
                finally:self.reader_log.close();self.reader_log=None
        finally:
            # A log-flush failure must not strand a guard after the child was
            # proven stopped. Propagate the log error after resource cleanup.
            os.close(self.lock_fd)
            self.lock_fd=None
            atexit.unregister(self.close)
        return True
