"""Bound the time a KD diagnostic is allowed to leave the guest stopped."""

from contextlib import contextmanager
import signal


@contextmanager
def deadline(seconds):
    """Raise ``TimeoutError`` after *seconds*, restoring prior SIGALRM state."""

    old_handler = signal.getsignal(signal.SIGALRM)
    old_timer = signal.getitimer(signal.ITIMER_REAL)

    def expired(_signum, _frame):
        raise TimeoutError("KD diagnostic deadline expired")

    signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, *old_timer)
        signal.signal(signal.SIGALRM, old_handler)
