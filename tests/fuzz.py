#!/usr/bin/env python3
"""Throw random panel frames and random/garbage Resolve packets at the daemon for a while.
Meant to be run against a sanitizer build (`make asan`), which must not report anything and the
daemon must stay alive.

    python3 tests/fuzz.py build/JLCooperPanelDaemon-asan [seconds]
"""
import os, random, socket, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import FakeEclipse, connect_uds, make_logdir, start_daemon, uds_in_use

exe = sys.argv[1] if len(sys.argv) > 1 else 'build/JLCooperPanelDaemon-asan'
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 25
logdir = make_logdir()

ecl = FakeEclipse()
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
os.environ.update(env)
# Garbage on the Resolve side makes the daemon drop that connection, so prefer the listening-socket mode
# (the fuzzer can reconnect). Fall back to the socketpair mode if /var/tmp/davinci_socket is taken.
mode = 'pair' if uds_in_use() else 'uds'
print('fuzzing in', mode, 'mode')
p, res, log = start_daemon(exe, ecl, mode, logdir, extra=('-d',))
time.sleep(2.5)
random.seed(7)
end = time.time() + seconds
n = 0
while time.time() < end and p.poll() is None:
    k = random.random()
    try:
        if k < 0.35:    # random panel frames
            b = bytes(random.choice([random.randrange(256), 0x55, 0x42, 0x46, 0x47, 0x48, 0x03])
                      for _ in range(random.randrange(0, 12)))
            ecl.conn.sendall(bytes([0xF0, 0x15, 0x25]) + bytes(x for x in b if x != 0xF7) + b'\xf7')
        elif k < 0.45:  # junk on the panel socket
            ecl.conn.sendall(bytes(random.randrange(256) for _ in range(random.randrange(1, 40))) + b'\xf7')
        elif k < 0.8:   # random Steinway packets
            ln = random.choice([0, 1, 2, 8, 0x22, 0x30, 100])
            res.sendall(struct.pack('<HHHHI', random.choice([0, 1, 2, 4, 3, 9]), random.randrange(0, 30),
                                    random.choice([0, 0x1000, 0x2000, 0x4000, 0x3000]), 0, ln)
                        + bytes(random.randrange(256) for _ in range(ln)))
        else:           # garbage / partial / absurd length
            res.sendall(bytes(random.randrange(256) for _ in range(random.randrange(1, 40))))
        n += 1
    except (OSError, AttributeError):
        # panel reconnecting, or the daemon dropped the Resolve connection (length guard)
        if mode == 'uds' and p.poll() is None:
            try:
                res = connect_uds()
            except RuntimeError:
                pass
    if random.random() < 0.01:
        time.sleep(0.05)
    try:
        res.settimeout(0.001)
        res.recv(65536)
    except Exception:
        pass

alive = p.poll() is None
print('sent', n, 'inputs; daemon alive:', alive)
if alive:
    p.terminate()
    p.wait(5)
text = open(log, errors='replace').read()
reports = text.count('AddressSanitizer') + text.count('runtime error')
print('sanitizer reports:', reports, '| exit code:', p.returncode, '| log:', log)
ecl.stop = True
sys.exit(0 if alive and reports == 0 and p.returncode == 0 else 1)
