#!/usr/bin/env python3
"""Decode the Wom1FwRx receipt (apple_agx_fw_rx_receipt.h, 304 bytes).

Usage: decode_fw_rx.py <Wom1FwRx.bin | kmd-receipts.txt>
A kmd-receipts text dump holds 'Wom1FwRx <hex>' lines; the last one wins.
Verdict: without the drain the read pointer never moves, so a full ring at
the last sample (write pointer one behind read) is stuck whether or not the
write pointer moved in earlier samples (EXP2216: 20 -> 255 once, then full);
its Last* stats values are stale from the fill time.
"""
import struct
import sys

FMT = '<16I6I6I16IQQQ4IQIIQQ4I4QQ'
NAMES = ('version bytes samples failures stats_read stats_write stats_write_first '
         'stats_write_changes stats_wrapped stats_full stats_unknown_tags stats_scanned '
         'ktrace_read ktrace_write ktrace_write_changes drain_failures').split()
TAGS = {0: 'power', 2: 'power_on', 3: 'power_off', 4: 'utilization', 9: 'avg_power',
        0xa: 'temperature', 0xb: 'power_state', 0xc: 'fw_busy', 0xd: 'pstate',
        0xe: 'temp_sensor'}
assert struct.calcsize(FMT) == 304


def decode(data):
    v = struct.unpack(FMT, data[:304])
    out = dict(zip(NAMES, v[:16]))
    out['fwlog_read'] = list(v[16:22])
    out['fwlog_write'] = list(v[22:28])
    out['stats_tags'] = {TAGS.get(i, 'tag%d' % i): n for i, n in enumerate(v[28:44]) if n}
    (out['first_qpc'], out['last_qpc'], out['util_ts'], *util) = v[44:51]
    out['util'] = util
    out['fw_busy_ts'], out['fw_busy'], out['power_state_active'] = v[51:54]
    out['power_state_ts'], out['power_state_last_busy'] = v[54:56]
    (out['power_state_off'], out['power_state_pstate'],
     out['pstate_min'], out['pstate_max']) = v[56:60]
    (out['drain_calls'], out['drained_stats'], out['drained_ktrace'],
     out['drained_fwlog']) = v[60:64]
    out['verdict'] = ('drained (Wom1FwRxDrain)' if out['drain_calls'] else
                      'stats ring full and stuck (values stale)' if out['stats_full'] else
                      'stats ring moving' if out['stats_write_changes'] else
                      'stats ring not full, not moving')
    return out


def load(path):
    if path.endswith('.bin'):
        return open(path, 'rb').read()
    data = None
    for line in open(path, errors='replace'):
        fields = line.split()
        if len(fields) >= 2 and fields[0] == 'Wom1FwRx':
            data = bytes.fromhex(fields[1])
    if data is None:
        raise SystemExit('no Wom1FwRx line')
    return data


if __name__ == '__main__':
    for key, value in decode(load(sys.argv[1])).items():
        print('%-22s %s' % (key, value))
