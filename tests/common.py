"""Shared helpers for the black-box tests: a fake Eclipse panel (TCP server) and a fake Resolve
(socketpair, or the daemon's unix socket)."""
import errno, os, select, socket, struct, subprocess, tempfile, threading, time

SOCK = '/var/tmp/davinci_socket'


class FakeEclipse:
    """Minimal Eclipse panel: accepts one TCP connection, records every F0 15 25 ... F7 frame the
    daemon sends, and answers the ID request."""

    def __init__(self):
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(('127.0.0.1', 0))
        self.srv.listen(1)
        self.port = self.srv.getsockname()[1]
        self.conn = None
        self.buf = b''
        self.frames = []
        self.lock = threading.Lock()
        self.stop = False
        self.t = threading.Thread(target=self.run, daemon=True)
        self.t.start()

    def run(self):
        while not self.stop:
            self.srv.settimeout(0.2)
            try:
                c, _ = self.srv.accept()
            except socket.timeout:
                continue
            self.conn = c
            self.buf = b''
            while not self.stop:
                r, _, _ = select.select([c], [], [], 0.2)
                if not r:
                    continue
                d = c.recv(4096)
                if not d:
                    break
                self.buf += d
                while b'\xf7' in self.buf:
                    f, self.buf = self.buf.split(b'\xf7', 1)
                    with self.lock:
                        self.frames.append(f + b'\xf7')
                    if f == b'\xf0\x15\x25\x03':  # ID request -> reply
                        c.sendall(b'\xf0\x15\x25\x03\x01\x02\x03\x04\x05\x07\xf7')
            self.conn = None

    def take(self):
        with self.lock:
            fr, self.frames = self.frames, []
        return fr

    def send(self, *b):
        self.conn.sendall(bytes([0xF0, 0x15, 0x25] + list(b) + [0xF7]))

    def drop(self):
        if self.conn:
            try:
                self.conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def rd(sock, timeout=1.0):
    """Read Steinway packets from the daemon until `timeout` (first packet) / 0.25 s of silence."""
    out = []
    data = b''
    end = time.time() + timeout
    first = True
    while True:
        now = time.time()
        if not first and now > end:
            break
        left = (end - now) if first else min(0.25, max(0.01, end - now))
        if left <= 0:
            break
        sock.settimeout(left)
        try:
            d = sock.recv(4096)
        except socket.timeout:
            break
        if not d:
            break
        data += d
        first = False
    while len(data) >= 12:
        n = struct.unpack_from('<I', data, 8)[0]
        if len(data) < 12 + n:
            break
        dest, cmd, tgt, rsv, ln = struct.unpack_from('<HHHHI', data, 0)
        out.append((dest, cmd, tgt, data[12:12 + n]))
        data = data[12 + n:]
    return out


def msg(dest, cmd, target, payload=b''):
    return struct.pack('<HHHHI', dest, cmd, target, 0, len(payload)) + payload


def uds_in_use():
    """True if something is listening on the daemon's well-known socket (e.g. a real Resolve
    session). The tests must never touch it."""
    if not os.path.exists(SOCK):
        return False
    s = socket.socket(socket.AF_UNIX)
    try:
        s.connect(SOCK)
        return True
    except OSError as e:
        if e.errno == errno.ECONNREFUSED:  # stale file left behind by a crashed daemon
            return False
        return True
    finally:
        s.close()


def connect_uds():
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(SOCK)
            return s
        except OSError:
            time.sleep(0.1)
    raise RuntimeError('daemon did not open ' + SOCK)


def start_daemon(exe, ecl, mode, logdir, extra=(), env_set=None, env_drop=(), default_config=False):
    """Start the daemon against the fake panel. mode 'pair': Resolve side is a socketpair passed
    with -s (this is how Resolve launches it). mode 'uds': the daemon listens on SOCK.
    Returns (process, resolve_side_socket, logfile)."""
    log = os.path.join(logdir, 'daemon_%s.log' % mode)
    env = dict(os.environ)
    if '-c' not in extra and not default_config:  # never pick up a real mapping file of the person running the tests
        env['JLCD_CONFIG'] = os.path.join(logdir, 'no-such-mapping.conf')
    if default_config:
        env.pop('JLCD_CONFIG', None)
    for k in env_drop:
        env.pop(k, None)
    env.update(env_set or {})
    if mode == 'uds':
        if os.path.exists(SOCK):
            os.unlink(SOCK)  # stale (checked with uds_in_use() by the caller)
        p = subprocess.Popen([exe, '-V', *extra, '-l', log, '127.0.0.1:%d' % ecl.port], env=env)
        return p, connect_uds(), log
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    p = subprocess.Popen([exe, '-V', *extra, '-l', log, '-s', str(b.fileno()), '127.0.0.1:%d' % ecl.port],
                         pass_fds=[b.fileno()], env=env)
    b.close()
    return p, a, log


def make_logdir():
    return tempfile.mkdtemp(prefix='jlcd-tests-')
