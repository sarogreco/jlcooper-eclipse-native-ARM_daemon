#!/usr/bin/env python3
"""Black-box tests: a fake Eclipse panel (TCP) and a fake Resolve (socketpair like the real launch with
`-s fd`, optionally the daemon's own unix socket) talk to the real daemon binary.

    python3 tests/test_daemon.py build/JLCooperPanelDaemon [--uds]

The tests are timing based (the daemon waits 1 s after connecting, shuttle repeats every ~110 ms, ...),
so run them on an otherwise idle machine. `--uds` also exercises the listening-socket mode on
/var/tmp/davinci_socket; it is skipped if something is already listening there (e.g. a running Resolve).
"""
import os, socket, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import FakeEclipse, SOCK, make_logdir, msg, rd, start_daemon, uds_in_use

args = [a for a in sys.argv[1:] if not a.startswith('--')]
EXE = args[0] if args else 'build/JLCooperPanelDaemon'
WITH_UDS = '--uds' in sys.argv
LOGDIR = make_logdir()
fails = 0
total = 0


def check(name, cond, extra=''):
    global fails, total
    total += 1
    print(('PASS ' if cond else 'FAIL ') + name + ('' if cond else '  ' + str(extra)))
    if not cond:
        fails += 1


def run(mode):
    print('=== mode:', mode)
    ecl = FakeEclipse()
    p, res, log = start_daemon(EXE, ecl, mode, LOGDIR)
    time.sleep(2.6)
    fr = ecl.take()
    check('setMode2 frame first', fr and fr[0] == bytes.fromhex('f01525' '0202020000' 'f7'), fr[:2])
    check('ID request second', len(fr) > 1 and fr[1] == bytes.fromhex('f01525' '03' 'f7'))
    check('all LEDs off (46 + shuttle)',
          sum(1 for f in fr if f[3:4] == b'\x55' and f[4:5] == b'\x03') + sum(1 for f in fr if f[3:4] == b'\x42') >= 40, len(fr))
    check('shuttle LEDs cleared BF6B00', bytes.fromhex('f01525bf6b00f7') in fr)
    check('7seg dashes', bytes.fromhex('f0152549' + '0202020202020202' + 'f7') in fr,
          [f.hex() for f in fr if f[3:4] == b'\x49'])
    check('mode-0 LED on (55 03 00 00)', bytes.fromhex('f0152555030000f7') in fr)
    check('LCD display2 info line', any(f[3:4] == b'\x43' for f in fr))
    check('LCD mode0 top line "Pan"', any(f[3:6] == b'\x55\x01\x00' and b'Pan' in f for f in fr))

    # --- status request
    res.sendall(msg(0, 10, 0)); r = rd(res)
    check('status reply connected', r == [(0, 10, 0, struct.pack('<HHHH', 0, 0, 0, 0))], r)
    # lcd image download then status
    payload = struct.pack('<H', 7) + b'\0' * 6 + struct.pack('<I', 4) + b'\0' * (0x22 - 12 + 4)
    res.sendall(msg(0, 1, 0, payload)); res.sendall(msg(0, 10, 0)); r = rd(res)
    check('status reply counts 1 image', r and r[0][3] == struct.pack('<HHHH', 0, 0, 1, 0), r)
    res.sendall(msg(0, 13, 0)); res.sendall(msg(0, 10, 0)); r = rd(res)
    check('reset clears image count', r and r[0][3] == struct.pack('<HHHH', 0, 0, 0, 0), r)

    # --- buttons -> keys (plain table: button 0x37 -> key 0x140)
    ecl.send(0x42, 0x37 - 0x17, 1); r = rd(res)
    check('button 0x37 -> key 0x140 (cmd3 tgt 0x2240)', r == [(0, 3, 0x2240, b'\x01\x00')], r)
    ecl.send(0x42, 0x37 - 0x17, 0); r = rd(res, 0.5)
    check('release sends nothing', r == [], r)
    # shift + button: 0x17 shifted -> key 0x2346 => prefix 0x3a5 then 0x346 (tgt 0x246)
    ecl.send(0x42, 0x2b - 0x17, 1); time.sleep(0.2)          # shift press
    ecl.send(0x42, 0x17 - 0x17, 1); r = rd(res)
    check('shifted button prefix + key', r == [(0, 3, 0x02a5, b'\x01\x00'), (0, 3, 0x0246, b'\x01\x00')], r)
    ecl.send(0x42, 0x2b - 0x17, 0); time.sleep(0.2)          # shift release
    # --- rotors
    ecl.send(0x55, 0x20, 5); r = rd(res)
    check('rotor1 +5 -> pot 0x300', r == [(0, 4, 0x0100, struct.pack('<h', 5))], r)
    ecl.send(0x55, 0x21, 0x7b); r = rd(res)
    check('rotor2 -5 (7-bit sign ext) -> pot 0x301', r == [(0, 4, 0x0101, struct.pack('<h', -5))], r)
    ecl.send(0x55, 0x2d + 0, 3); r = rd(res)     # rotor 7 = TB1 ring
    check('TB1 ring -> cmd4 tgt 0x1310', r == [(0, 4, 0x1310, struct.pack('<h', 3))], r)
    ecl.send(0x55, 0x2d + 1, 4); r = rd(res)     # rotor 8 = TB1 vertical
    check('TB1 vertical -> cmd5 {0,-4}', r == [(0, 5, 0x1300, struct.pack('<hh', 0, -4))], r)
    ecl.send(0x55, 0x2d + 2, 4); r = rd(res)     # rotor 9 = horizontal
    check('TB1 horizontal -> cmd5 {-4,0}', r == [(0, 5, 0x1300, struct.pack('<hh', -4, 0))], r)
    ecl.send(0x55, 0x2d + 4, 4); r = rd(res)     # rotor 11 = TB2 vertical
    check('TB2 vertical tgt 0x1301', r == [(0, 5, 0x1301, struct.pack('<hh', 0, -4))], r)
    ecl.send(0x47, 6); r = rd(res)
    check('jog -> cmd4 tgt 0x400', r == [(0, 4, 0x0400, struct.pack('<h', 6))], r)
    ecl.send(0x46, 0, 2); r = rd(res)
    check('encoder7 + -> key 0x3a1', r == [(0, 3, 0x02a1, b'\x01\x00')], r)
    ecl.send(0x46, 3, 0x7f); r = rd(res)     # encoder10 negative -> base 0x23b2: prefix 0x3a5 + key 0x3b2
    check('encoder10 - -> prefix + 0x3b2', r == [(0, 3, 0x02a5, b'\x01\x00'), (0, 3, 0x02b2, b'\x01\x00')], r)
    # --- mode switch (button 3)
    ecl.take()
    ecl.send(0x55, 3, 0); time.sleep(0.4); fr = ecl.take()
    check('mode switch: led0 off, led3 on',
          bytes.fromhex('f0152555030001f7') in fr and bytes.fromhex('f0152555030300f7') in fr, [f.hex() for f in fr])
    check('mode switch: LCD updated', any(b'Zoom' in f for f in fr))
    ecl.send(0x55, 0x20, 2); r = rd(res)        # mode 3, rotor 1 -> pot on panel 1
    check('rotor1 in mode3 -> pot 0x100 (tgt 0x2100)', r == [(0, 4, 0x2100, struct.pack('<h', 2))], r)
    # knob reset (plain button 0xf -> rotor 1)
    ecl.send(0x55, 0x26, 0); r = rd(res)
    check('knob reset msg cmd6', r == [(0, 6, 0x2100, b'\x01\x00')], r)
    # plusminus
    ecl.send(0x42, 0x49 - 0x17, 1); r = rd(res)
    check('plusminus first -> key 0x3a8', r == [(0, 3, 0x02a8, b'\x01\x00')], r)
    ecl.send(0x42, 0x49 - 0x17, 1); r = rd(res)
    check('plusminus second -> key 0x3a6', r == [(0, 3, 0x02a6, b'\x01\x00')], r)
    # --- shuttle
    ecl.take(); ecl.send(0x48, 10); time.sleep(0.45); fr = ecl.take(); r = rd(res, 0.3)
    check('shuttle LED BF6B02', bytes.fromhex('f01525bf6b02f7') in fr, [f.hex() for f in fr])
    jogs = [m for m in r if m[1] == 4 and m[2] == 0x400]
    check('shuttle repeats +1 jog (~every 110ms)',
          2 <= len(jogs) <= 6 and all(j[3] == struct.pack('<h', 1) for j in jogs), len(jogs))
    ecl.send(0x48, 0); time.sleep(0.2); rd(res, 0.2); fr = ecl.take()
    check('shuttle center clears LED', bytes.fromhex('f01525bf6b00f7') in fr)
    r = rd(res, 0.4)
    check('no more jogs after center', r == [], r)
    # --- panel disconnect -> status absent, then auto reconnect
    ecl.drop(); time.sleep(0.5)
    res.sendall(msg(0, 10, 0)); r = rd(res)
    check('status absent when panel gone', r and r[0][3][:2] == b'\x01\x00', r)
    time.sleep(4.2); fr = ecl.take()
    check('daemon reconnects & re-inits', bytes.fromhex('f01525' '0202020000' 'f7') in fr, [f.hex() for f in fr][:3])
    res.sendall(msg(0, 10, 0)); r = rd(res)
    check('status connected again', r and r[0][3][:2] == b'\x00\x00', r)
    # unknown cmd must not crash
    res.sendall(msg(0, 5, 0, b'abc')); res.sendall(msg(4, 10, 0)); r = rd(res)
    check('broadcast status reply x3 and no crash', len(r) == 3 and p.poll() is None, (len(r), p.poll()))
    p.terminate(); p.wait(timeout=3)
    check('clean exit on SIGTERM', p.returncode == 0, p.returncode)
    if mode == 'uds':
        check('socket file removed', not os.path.exists(SOCK))
    ecl.stop = True
    res.close()


def smoke():
    """Command line behaviour that does not need a panel."""
    import subprocess
    print('=== command line')
    o = subprocess.run([EXE, '-v'], capture_output=True, text=True, timeout=5)
    check('-v prints "<argv0> version 7.1" first and exits 0',
          o.returncode == 0 and o.stdout.splitlines()[0].endswith(' version 7.1'), o)
    check('-v does not claim to be Blackmagic software', 'Copyright Blackmagic' not in o.stdout, o.stdout)
    o = subprocess.run([EXE, '-h'], capture_output=True, text=True, timeout=5)
    check('-h prints usage and exits 0', o.returncode == 0 and 'Usage:' in o.stdout, o)
    o = subprocess.run([EXE, '-Z'], capture_output=True, text=True, timeout=5)
    check('unknown option exits non-zero', o.returncode != 0, o.returncode)


smoke()
run('pair')
if WITH_UDS:
    if uds_in_use():
        print('SKIP uds mode: something is already listening on', SOCK)
    else:
        run('uds')
print('logs in', LOGDIR)
print('CHECKS: %d  FAILS: %d' % (total, fails))
sys.exit(1 if fails else 0)
