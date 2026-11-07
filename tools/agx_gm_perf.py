#!/usr/bin/env python3
"""Machine-readable GravityMark performance summary from a D3D12 UMD trace.

Usage:
  agx_gm_perf.py summary UMD.log [--skip 10] [--window 45] [--err ERR.txt] [--json OUT]
  agx_gm_perf.py compare BEFORE.json AFTER.json [--noise 1.0]

GravityMark 1.89 has no frame-count or duration option (its option table:
benchmark, close, count, times, stats, ...); a FAST/PERF run is the normal
benchmark closed by agx_gravitymark_run.ps1 at its timeout. The benchmark
camera path is time-based, so the same window after the first present
covers the same scenes in every run; the score is the number of frames of
the full ~167 s run (EXP2134: 6257 points, 6258 frames).

Frame = interval between two measure-d3d12-present rows of the process with
the most presents. The loading screen presents too, and the first benchmark
frames compile shaders (gaps of 0.5-9 s, MESA_SHADER_CACHE_DISABLE runs), so
the anchor is the present after the last gap of at least 1 s within the
first 60 s; the window starts --skip seconds after the anchor and lasts
--window seconds (0 = to the end). GPU figures come from
measure-gpu-stages (job firmware start/end/TA start/TA end, 24 MHz) and
measure-gpu-prelude rows, for jobs that end inside the window:
  busy        union of [start, end] of jobs with a valid start / their span,
              null when fewer than 90 % of the jobs have one (render starts
              need Wom1TaStartTimestamps, EXP2126);
  c_to_ta     previous compute end -> render TA start (G2, EXP2126);
  c_to_pre    previous compute end -> render prelude start (EXP2129);
  r_to_c      previous render end -> compute start (EXP2131).
A TA start is valid only with the Wom1TaStartTimestamps receipt (EXP2126);
without it c_to_ta is reported as null.

compare: mean frame time (frames / time, the score's own measure) of BEFORE
and AFTER; the window is split into eight equal blocks and the verdict needs
both a change larger than --noise percent (run-to-run noise between boots is
about 1 %, EXP2124-2134) and at least seven of eight AFTER blocks on the same
side of the BEFORE block covering the same benchmark time, i.e. the same
scenes (paired sign test, p < 0.04).

Window fidelity (offline, EXP2124/2126/2129/2131/2134 HD logs): skip 10 s +
60 s keeps the order of the full scores and shows about 0.7x of their
change (+0.9 % vs +1.3 %, +2.9 % vs +4.1 %); 90 s about 0.95x; 45 s does
not keep the order (EXP2129 vs EXP2131). PERF therefore uses 10 + 60 s.
"""
import argparse
import json
import statistics
import sys

DEFAULT_HZ = 24_000_000
BLOCKS = 8


def _u64(words, i):
    return words[i] | (words[i + 1] << 32)


def parse(lines):
    presents, stages, preludes, hz = {}, {}, {}, set()
    rejects = {}
    for line in lines:
        if line.startswith('reject'):
            tag = line.split(' ', 1)[0]
            rejects[tag] = rejects.get(tag, 0) + 1
            continue
        if not line.startswith('measure-'):
            continue
        fields = line.split()
        tag = fields[0]
        if tag not in ('measure-d3d12-present', 'measure-gpu-stages',
                       'measure-gpu-prelude', 'measure-gpu-completion'):
            continue
        meta = dict(f.split('=', 1) for f in fields[1:] if '=' in f)
        try:
            pid, qpc = int(meta['pid']), int(meta['qpc'])
            words = [int(x, 16) for x in fields[1:] if '=' not in x]
        except (KeyError, ValueError):
            continue
        if tag == 'measure-d3d12-present':
            presents.setdefault(pid, []).append(qpc)
        elif tag == 'measure-gpu-stages' and len(words) == 10:
            stages.setdefault(pid, {})[_u64(words, 0)] = tuple(
                _u64(words, i) for i in (2, 4, 6, 8))
        elif tag == 'measure-gpu-prelude' and len(words) == 6:
            preludes.setdefault(pid, {})[_u64(words, 0)] = (_u64(words, 2),
                                                            _u64(words, 4))
        elif tag == 'measure-gpu-completion' and len(words) == 13 and _u64(words, 11):
            hz.add(_u64(words, 11))
    return presents, stages, preludes, (hz.pop() if len(hz) == 1 else DEFAULT_HZ), rejects


def _quant(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int(q * len(values)))] if values else None


def _stat(values):
    if not values:
        return None
    return dict(n=len(values), median=round(statistics.median(values), 4),
                p10=round(_quant(values, 0.1), 4), p90=round(_quant(values, 0.9), 4),
                total=round(sum(values), 3))


def summarize(lines, skip=10.0, window=60.0, err_text=''):
    presents, stages, preludes, hz, rejects = parse(lines)
    if not presents:
        raise ValueError('no measure-d3d12-present rows')
    pid = max(presents, key=lambda p: len(presents[p]))
    q = sorted(presents[pid])
    anchor = q[0]
    for a, b in zip(q, q[1:]):
        if a - q[0] > 60 * hz:
            break
        if b - a >= hz:
            anchor = b
    begin = anchor + int(skip * hz)
    end = begin + int(window * hz) if window else q[-1]
    frames = [(b - a) * 1000.0 / hz for a, b in zip(q, q[1:]) if begin <= a and b <= end]
    if len(frames) < BLOCKS:
        raise ValueError('window holds %d frames, need at least %d' % (len(frames), BLOCKS))
    size = len(frames) // BLOCKS
    blocks = [statistics.fmean(frames[i * size:(i + 1) * size]) for i in range(BLOCKS)]

    S, P = stages.get(pid, {}), preludes.get(pid, {})
    seqs = sorted(S)
    # The firmware clock runs at the QPC frequency with an unknown offset
    # (EXP1112), so jobs are not placed on the present clock: the window
    # takes the same fraction of the run's completed jobs (sequence order)
    # as the frame window takes of the run's presents.
    in_window = [s for s in seqs if S[s][1]]
    if in_window:
        lo = int(len(in_window) * (begin - q[0]) / max(1, q[-1] - q[0]))
        hi = int(len(in_window) * (end - q[0]) / max(1, q[-1] - q[0]))
        in_window = in_window[lo:hi]
    # A start is valid within 1 s of its end: render 3D starts are stale on
    # G13 (EXP1112); a render's own span starts at its TA start when valid.
    def job_start(s):
        start, finish, ta_start, ta_end = S[s]
        if ta_end and ta_start and 0 < finish - ta_start < hz:
            return ta_start
        return start if start and 0 < finish - start < hz else 0
    spans = sorted((job_start(s), S[s][1]) for s in in_window if job_start(s))
    covered = len(spans) / len(in_window) if in_window else 0.0
    busy = 0
    cur_a = cur_b = None
    for a, b in spans:
        if cur_b is None or a > cur_b:
            if cur_b is not None:
                busy += cur_b - cur_a
            cur_a, cur_b = a, b
        else:
            cur_b = max(cur_b, b)
    if cur_b is not None:
        busy += cur_b - cur_a
    span = (max(b for _, b in spans) - min(a for a, _ in spans)) if spans else 0
    ms = lambda d: d * 1000.0 / hz
    c_ta, c_pre, r_c, ta_valid = [], [], [], 0
    window_set = set(in_window)
    for prev, cur in zip(seqs, seqs[1:]):
        if cur != prev + 1 or cur not in window_set:
            continue
        p_end, (c_start, _c_end, ta_s, _ta_e) = S[prev][1], S[cur]
        prev_render = bool(S[prev][3])
        if S[cur][3]:  # render job (TA end valid)
            if not prev_render and p_end:
                if ta_s and abs(ta_s - p_end) < hz:  # stale TA starts are minutes away
                    c_ta.append(ms(ta_s - p_end)); ta_valid += 1
                if cur in P and P[cur][0]:
                    c_pre.append(ms(P[cur][0] - p_end))
        elif c_start and p_end and prev_render:
            r_c.append(ms(c_start - p_end))
    removed = 'device removed' in err_text.lower()
    return dict(
        version=1, pid=pid, hz=hz, skip_s=skip, window_s=window,
        anchor_s=round((anchor - q[0]) / hz, 3),
        presents=len(q), frames=len(frames),
        frame_ms=dict(median=round(statistics.median(frames), 4),
                      p95=round(_quant(frames, 0.95), 4),
                      mean=round(statistics.fmean(frames), 4)),
        fps=round(1000.0 / statistics.fmean(frames), 3),
        block_means_ms=[round(b, 4) for b in blocks],
        gpu=dict(jobs=len(in_window), start_coverage=round(covered, 4),
                 busy_fraction=round(busy / span, 4) if span and covered >= 0.9 else None,
                 c_to_ta_ms=_stat(c_ta) if ta_valid else None,
                 c_to_prelude_ms=_stat(c_pre), r_to_c_ms=_stat(r_c)),
        errors=dict(device_removed=removed, rejects=rejects),
    )


def compare(before, after, noise=1.0):
    b, a = before['frame_ms']['mean'], after['frame_ms']['mean']
    change = (a - b) / b * 100.0
    pairs = list(zip(before['block_means_ms'], after['block_means_ms']))
    below = sum(1 for x, y in pairs if y < x)
    above = sum(1 for x, y in pairs if y > x)
    n = len(pairs)
    if abs(change) <= noise:
        verdict = 'no-significant-change'
    elif change < 0 and below >= n - 1:
        verdict = 'faster'
    elif change > 0 and above >= n - 1:
        verdict = 'slower'
    else:
        verdict = 'inconclusive'
    same_window = (before['skip_s'], before['window_s']) == (after['skip_s'], after['window_s'])
    errors = after['errors']['device_removed'] or before['errors']['device_removed']
    return dict(version=1, before_mean_ms=b, after_mean_ms=a,
                change_percent=round(change, 3), noise_percent=noise,
                blocks_faster=below, blocks_slower=above, blocks=n,
                verdict=verdict if same_window and not errors else 'invalid',
                same_window=same_window, device_removed=errors)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('summary')
    s.add_argument('log'); s.add_argument('--skip', type=float, default=10.0)
    s.add_argument('--window', type=float, default=60.0)
    s.add_argument('--err'); s.add_argument('--json')
    c = sub.add_parser('compare')
    c.add_argument('before'); c.add_argument('after')
    c.add_argument('--noise', type=float, default=1.0)
    args = ap.parse_args(argv)
    if args.cmd == 'summary':
        with open(args.log, errors='replace') as handle:
            err = open(args.err, errors='replace').read() if args.err else ''
            out = summarize(handle, args.skip, args.window, err)
        if args.json:
            with open(args.json, 'w') as handle:
                json.dump(out, handle, indent=1)
    else:
        out = compare(json.load(open(args.before)), json.load(open(args.after)), args.noise)
    json.dump(out, sys.stdout, indent=1)
    print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
