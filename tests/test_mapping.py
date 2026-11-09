#!/usr/bin/env python3
"""Tests for the optional mapping file: SHIFT layer on the knobs, LCD labels while SHIFT is held,
live reload, error handling and the probe mode.

    python3 tests/test_mapping.py build/JLCooperPanelDaemon
"""
import os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import FakeEclipse, make_logdir, msg, rd, start_daemon

EXE = sys.argv[1] if len(sys.argv) > 1 else 'build/JLCooperPanelDaemon'
LOGDIR = make_logdir()
CFG = os.path.join(LOGDIR, 'mapping.conf')
fails = 0
total = 0

SEG = {' ': 0x00, '0': 0x7d, '1': 0x28, '2': 0x37, '3': 0x2f, '4': 0x6a, '5': 0x4f, '6': 0x5f, '7': 0x29, '8': 0x7f, '9': 0x6f,
       '-': 0x02}


def check(name, cond, extra=''):
    global fails, total
    total += 1
    print(('PASS ' if cond else 'FAIL ') + name + ('' if cond else '  ' + str(extra)))
    if not cond:
        fails += 1


def seg_frame(text):
    s = (text + ' ' * 8)[:8]
    return bytes([0xF0, 0x15, 0x25, 0x49] + [SEG[s[i]] for i in range(7, -1, -1)] + [0xF7])


def pot(panel, ctrl, v):          # Steinway pot packet as read by rd()
    return (0, 4, ctrl | 0x100 | {0: 0, 1: 0x2000, 2: 0x1000}[panel], struct.pack('<h', v))


def write_cfg(text):
    with open(CFG, 'w') as f:
        f.write(text)


def logtext(path):
    with open(path, errors='replace') as f:
        return f.read()


def lcd(fr, *needles):
    return any(f[3:5] == b'\x55\x01' and all(n in f for n in needles) for f in fr)


def c8(text):                      # the daemon centres a label in its 8-character column
    s = text[:8]
    pad = (8 - len(s)) // 2
    return (' ' * pad + s + ' ' * (8 - len(s) - pad)).encode()


def lcd_at(fr, disp, col, line, text):
    # 55 01 <disp<<6 | col | line<<5> <len> <chars>
    return bytes([0xF0, 0x15, 0x25, 0x55, 0x01, (disp << 6) | col | (line << 5), 8]) + c8(text) + b'\xf7' in fr


def shift(ecl, down):
    ecl.send(0x42, 0x2b - 0x17, 1 if down else 0)
    time.sleep(0.3)


write_cfg("""# test mapping
shiftpot   2 1 2/200
shiftpot   2 2 2/201
shiftpot   2 3 0x2ca          # raw id form, = 2/202
shiftlabel 2 1 Color|Temp
shiftlabel 2 2 Tint|
shiftlabel 2 3 High|lights
shiftlabel 2 4 Dsp1|Chk
""")
ecl = FakeEclipse()
p, res, log = start_daemon(EXE, ecl, 'pair', LOGDIR, extra=('-c', CFG))
time.sleep(2.6)
ecl.take()
check('config loaded at start-up is reported in the log (without -V needed)', 'mapping: loaded' in logtext(log) and '3 SHIFT knob(s), 4 label(s), probe off' in logtext(log), logtext(log)[:300])

# --- mode 2 (third mode key): Red/Green/Blue Offset
ecl.send(0x55, 2, 0); time.sleep(0.4); fr = ecl.take()
check('mode 2 shows the normal labels', lcd(fr, b'Red') and lcd(fr, b'Offset'), [f.hex() for f in fr][:4])
ecl.send(0x55, 0x20, 5); r = rd(res)
check('no SHIFT: knob 1 = pot 0x25b (2/91)', r == [pot(2, 0x5b, 5)], r)

# --- SHIFT layer
shift(ecl, True); fr = ecl.take()
check('SHIFT draws knob 1 on display 0, column 0 (upper line / lower line)', lcd_at(fr, 0, 0, 0, 'Color') and lcd_at(fr, 0, 0, 1, 'Temp'), [f.hex() for f in fr])
check('SHIFT draws knobs 2 and 3 on display 0, columns 1 and 2', lcd_at(fr, 0, 8, 0, 'Tint') and lcd_at(fr, 0, 8, 1, '') and lcd_at(fr, 0, 16, 0, 'High') and lcd_at(fr, 0, 16, 1, 'lights'), [f.hex() for f in fr])
check('SHIFT draws knob 4 on display 1, column 0', lcd_at(fr, 1, 0, 0, 'Dsp1') and lcd_at(fr, 1, 0, 1, 'Chk'), [f.hex() for f in fr])
ecl.send(0x55, 0x20, 4); r = rd(res)
check('SHIFT + knob 1 -> 2/200', r == [pot(2, 200, 4)], r)
ecl.send(0x55, 0x21, 0x7d); r = rd(res)
check('SHIFT + knob 2 (negative) -> 2/201', r == [pot(2, 201, -3)], r)
ecl.send(0x55, 0x22, 6); r = rd(res)
check('SHIFT + knob 3 (raw 0x2ca) -> 2/202', r == [pot(2, 202, 6)], r)
ecl.send(0x55, 0x23, 2); r = rd(res)
check('SHIFT + knob 4 (no SHIFT function) keeps its normal pot 0x25a', r == [pot(2, 0x5a, 2)], r)
ecl.send(0x55, 0x26, 0); r = rd(res)     # knob 1 push button while SHIFT is held
check('SHIFT + knob 1 push resets the SHIFT function', r == [(0, 6, 0x1000 | 0x100 | 200, b'\x01\x00')], r)
shift(ecl, False); fr = ecl.take()
check('releasing SHIFT restores the normal labels', lcd(fr, b'Red') and lcd(fr, b'Green') and not lcd(fr, b'Color'), [f.hex() for f in fr])
ecl.send(0x55, 0x20, 5); r = rd(res)
check('after SHIFT: knob 1 back to 2/91', r == [pot(2, 0x5b, 5)], r)
ecl.send(0x55, 0x26, 0); r = rd(res)
check('plain knob 1 push still resets 2/91', r == [(0, 6, 0x1000 | 0x100 | 0x5b, b'\x01\x00')], r)

# --- other modes are untouched by a mode-2 SHIFT layer
ecl.send(0x55, 0, 0); time.sleep(0.3); ecl.take()
shift(ecl, True); fr = ecl.take()
check('mode 0 + SHIFT: no label change', not lcd(fr, b'Color'), [f.hex() for f in fr])
ecl.send(0x55, 0x20, 1); r = rd(res)
check('mode 0 + SHIFT: knob 1 = normal pot 0x300', r == [pot(0, 0, 1)], r)
shift(ecl, False); ecl.take()
ecl.send(0x55, 2, 0); time.sleep(0.3); ecl.take()

# --- live reload (same-size edit within the same second must be noticed)
write_cfg(open(CFG).read().replace('2/200', '2/210'))
time.sleep(1.6)
check('reload is reported', logtext(log).count('mapping: loaded') >= 2, logtext(log)[-300:])
shift(ecl, True); ecl.take()
ecl.send(0x55, 0x20, 4); r = rd(res)
check('edited file takes effect without restart (2/210)', r == [pot(2, 210, 4)], r)
shift(ecl, False)

# --- bad lines are reported and skipped, the rest still works
with open(CFG, 'a') as f:
    f.write("bogus 1 2 3\nshiftpot 9 9 9\nshiftpot 2 4 3/1\nshiftlabel 2 x\nprobe maybe\n" + 'x' * 1500 + "\nshiftpot 2 4 1/7\n")
time.sleep(1.6)
t = logtext(log)
check('bad lines are reported', t.count('ERROR: ') >= 5 and "unknown directive 'bogus'" in t, t[-600:])
check('daemon survived the bad file', p.poll() is None)
shift(ecl, True); ecl.take()
ecl.send(0x55, 0x23, 2); r = rd(res)
check('good lines after bad ones are still applied (knob 4 -> 1/7)', r == [pot(1, 7, 2)], r)
ecl.send(0x55, 0x20, 4); r = rd(res)
check('and earlier good lines too', r == [pot(2, 210, 4)], r)
shift(ecl, False); ecl.take()

# --- file removed: back to the built-in mapping
os.unlink(CFG)
time.sleep(1.6)
check('removal is reported', 'is gone' in logtext(log), logtext(log)[-300:])
shift(ecl, True); fr = ecl.take()
ecl.send(0x55, 0x20, 4); r = rd(res)
check('without a file SHIFT does not change the knob', r == [pot(2, 0x5b, 4)], r)
shift(ecl, False); ecl.take()

# --- probe mode
write_cfg("probe on\nprobestart 2/80\n")
time.sleep(1.6)
fr = ecl.take()
check('probe on: 7-segment shows panel 2 control 080', seg_frame('2   080 ') in fr, [f.hex() for f in fr if f[3:4] == b'\x49'])
check('probe on is reported', 'probe ON' in logtext(log))
ecl.send(0x55, 0x25, 1); r = rd(res, 0.5); fr = ecl.take()
check('probe: knob 6 up -> 081, nothing sent to Resolve', r == [] and seg_frame('2   081 ') in fr, (r, [f.hex() for f in fr if f[3:4] == b'\x49']))
ecl.send(0x55, 0x25, 1); r = rd(res, 0.5); fr = ecl.take()
check('probe: knob 6 up again -> 082', r == [] and seg_frame('2   082 ') in fr, (r, [f.hex() for f in fr if f[3:4] == b'\x49']))
ecl.send(0x55, 0x25, 0x7f); r = rd(res, 0.5); fr = ecl.take()
check('probe: knob 6 down -> 081', r == [] and seg_frame('2   081 ') in fr, (r, [f.hex() for f in fr if f[3:4] == b'\x49']))
shift(ecl, True); ecl.take()
ecl.send(0x55, 0x25, 1); r = rd(res, 0.5); fr = ecl.take()
check('probe: SHIFT + knob 6 steps by 16 -> 097', r == [] and seg_frame('2   097 ') in fr, (r, [f.hex() for f in fr if f[3:4] == b'\x49']))
shift(ecl, False); ecl.take()
ecl.send(0x55, 0x24, 3); r = rd(res)
check('probe: knob 5 sends to the id under test (2/97)', r == [pot(2, 97, 3)], r)
ecl.send(0x55, 0x2a, 0); r = rd(res)     # knob 5 push button (0x13)
check('probe: knob 5 push resets the id under test', r == [(0, 6, 0x1000 | 0x100 | 97, b'\x01\x00')], r)
ecl.send(0x55, 0x20, 5); r = rd(res)
check('probe: the other knobs keep working', r == [pot(2, 0x5b, 5)], r)
write_cfg("probe on\nprobestart 0/0\n")
time.sleep(1.6); ecl.take()
ecl.send(0x55, 0x25, 0x7f); r = rd(res, 0.5); fr = ecl.take()
check('probe: wraps below 0/0 to 2/255', seg_frame('2   255 ') in fr, [f.hex() for f in fr if f[3:4] == b'\x49'])
ecl.send(0x55, 0x25, 1); time.sleep(0.3); fr = ecl.take()
check('probe: and wraps back to 0/0', seg_frame('0   000 ') in fr, [f.hex() for f in fr if f[3:4] == b'\x49'])
write_cfg("probe off\n")
time.sleep(1.6); fr = ecl.take()
check('probe off: 7-segment back to dashes', seg_frame('--------') in fr, [f.hex() for f in fr if f[3:4] == b'\x49'])
ecl.send(0x55, 0x24, 3); r = rd(res)
check('probe off: knob 5 is the normal pot again (mode 2 -> 0x251)', r == [pot(2, 0x51, 3)], r)

# --- one-shot commands for scripts: send / reset
def atomic_cfg(text):                 # how a script should write the file: temp file + rename
    tmp = CFG + '.tmp'
    with open(tmp, 'w') as f:
        f.write(text)
    os.replace(tmp, CFG)


atomic_cfg("probe on\nprobestart 2/9\n")
time.sleep(0.8); ecl.take(); rd(res, 0.3)
t0 = time.time()
atomic_cfg("probe on\nprobestart 2/9\nsend 2/33 20\n")
r = rd(res, 1.5)
check('send: delta goes to Resolve right after the file changes', r == [pot(2, 33, 20)], r)
check('send: within ~0.6 s of the write (the file is polled every 0.2 s)', time.time() - t0 < 2.0)
check('send: the 7-segment shows the id', seg_frame('2   033 ') in ecl.take())
atomic_cfg("probe on\nprobestart 2/9\nsend 2/33 -20\n")
r = rd(res, 1.5)
check('send: negative delta (undo)', r == [pot(2, 33, -20)], r)
atomic_cfg("probe on\nprobestart 2/9\nreset 0x221\nsend 1/7 3\n")
r = rd(res, 1.5)
check('reset and several commands run in order', r == [(0, 6, 0x1000 | 0x100 | 33, b'\x01\x00'), pot(1, 7, 3)], r)
atomic_cfg("probe on\nprobestart 2/9\nsend 2/33 5\n")
r = rd(res, 1.5)
check('the same file content written again runs again (every change of the file counts)', r == [pot(2, 33, 5)], r)
atomic_cfg("probe off\nsend 2/33 7\n")
r = rd(res, 1.2)
check('send is ignored unless probe is on', r == [], r)
atomic_cfg("probe on\nsend 2/33 99999\nsend zz 1\nreset\n")
r = rd(res, 1.2)
check('bad send/reset lines run nothing and are reported', r == [] and logtext(log).count('expected') >= 3, (r, logtext(log)[-300:]))
atomic_cfg("probe off\n")
time.sleep(0.8); rd(res, 0.2); ecl.take()

# --- panel reconnect keeps the mapping and probe state
write_cfg("shiftpot 2 1 2/200\nshiftlabel 2 1 Color|Temp\nprobe on\nprobestart 2/9\n")
time.sleep(1.6); ecl.take()
ecl.drop(); time.sleep(4.5); fr = ecl.take()
check('after a panel reconnect the probe id is shown again', seg_frame('2   009 ') in fr, [f.hex() for f in fr if f[3:4] == b'\x49'])

p.terminate(); p.wait(timeout=3)
check('clean exit on SIGTERM', p.returncode == 0, p.returncode)
ecl.stop = True
res.close()

# --- default location of the file: $HOME/Library/Application Support/JLCooperPanelDaemon/mapping.conf
import pwd
home = os.path.join(LOGDIR, 'home')
cfgdir = os.path.join(home, 'Library', 'Application Support', 'JLCooperPanelDaemon')
os.makedirs(cfgdir)
with open(os.path.join(cfgdir, 'mapping.conf'), 'w') as f:
    f.write('probe on\nprobestart 2/33\n')
ecl2 = FakeEclipse()
p2, res2, log2 = start_daemon(EXE, ecl2, 'pair', LOGDIR, env_set={'HOME': home}, default_config=True)
time.sleep(2.6)
check('default path under $HOME is used', 'mapping: loaded ' + os.path.join(cfgdir, 'mapping.conf') in logtext(log2) and 'probe ON' in logtext(log2), logtext(log2)[:300])
check('and probe mode is active (7-segment shows 2   033)', seg_frame('2   033 ') in ecl2.take())
p2.terminate(); p2.wait(timeout=3); ecl2.stop = True; res2.close()

# --- Resolve may start the helper without HOME: fall back to the passwd entry, and say where it looked
ecl3 = FakeEclipse()
p3, res3, log3 = start_daemon(EXE, ecl3, 'pair', LOGDIR, env_drop=('HOME',), default_config=True)
time.sleep(1.5)
expect = os.path.join(pwd.getpwuid(os.getuid()).pw_dir, 'Library', 'Application Support', 'JLCooperPanelDaemon', 'mapping.conf')
check('without HOME the passwd home directory is used and logged', expect in logtext(log3) and p3.poll() is None, logtext(log3)[:300])
p3.terminate(); p3.wait(timeout=3); ecl3.stop = True; res3.close()

# --- a send line left in the file does not fire when the daemon starts
cfg5 = os.path.join(LOGDIR, 'leftover.conf')
with open(cfg5, 'w') as f:
    f.write('probe on\nsend 2/33 20\n')
ecl5 = FakeEclipse()
p5, res5, log5 = start_daemon(EXE, ecl5, 'pair', LOGDIR, extra=('-c', cfg5))
time.sleep(2.0)
r = rd(res5, 0.6)
check('a leftover send line does not run at start-up', r == [], r)
p5.terminate(); p5.wait(timeout=3); ecl5.stop = True; res5.close()

# --- no file at start-up is reported, and the file is picked up when it appears
ecl4 = FakeEclipse()
late = os.path.join(LOGDIR, 'late.conf')
p4, res4, log4 = start_daemon(EXE, ecl4, 'pair', LOGDIR, extra=('-c', late))
time.sleep(1.5)
check('missing file at start-up is reported with its path', 'mapping: no file at ' + late in logtext(log4), logtext(log4)[:300])
with open(late, 'w') as f:
    f.write('probe on\n')
time.sleep(2.0)
check('and picked up when it appears', 'mapping: loaded ' + late in logtext(log4), logtext(log4)[:400])
p4.terminate(); p4.wait(timeout=3); ecl4.stop = True; res4.close()

print('logs in', LOGDIR)
print('CHECKS: %d  FAILS: %d' % (total, fails))
sys.exit(1 if fails else 0)
