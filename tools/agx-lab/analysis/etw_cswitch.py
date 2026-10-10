"""Parse context switches of one thread from an xperf dumper CSV (builder/xperf-dump.ps1).

cswitch_events(path, tid) returns [(wake_ts, wait_us, stack)]: every switch-in of
the thread, with the time it was switched out and the switch-in stack joined by
' < '. The DWM tools below take the composition thread's tid (the thread whose
stacks show WaitForNextTick).
"""


def cswitch_events(path, tid):
    events = []
    pend = None
    frames = []
    out_ts = None

    def flush():
        if pend:
            events.append((pend[0], pend[1], ' < '.join(frames)))

    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            s = line.lstrip()
            if s.startswith('Stack,'):
                if pend:
                    p = s.split(',', 5)
                    if len(p) >= 6 and p[2].strip().isdigit() and int(p[2]) == tid:
                        frames.append(p[5].strip())
                continue
            if not s.startswith('CSwitch,'):
                continue
            flush()
            pend = None
            frames = []
            p = [x.strip() for x in s.split(',')]
            if len(p) < 14 or not p[1].isdigit():
                continue
            ts = int(p[1])
            newtid, oldtid = int(p[3]), int(p[9])
            if oldtid == tid:
                out_ts = ts
            if newtid == tid and out_ts is not None:
                pend = (ts, ts - out_ts)
    flush()
    return events


def frame_bounds(events):
    """Composition frames: from one wake out of WaitForNextTick to the next."""
    starts = [i for i, e in enumerate(events) if 'WaitForNextTick' in e[2]]
    for a, b in zip(starts, starts[1:]):
        yield a, b, events[a][0], events[b][0] - events[b][1]
