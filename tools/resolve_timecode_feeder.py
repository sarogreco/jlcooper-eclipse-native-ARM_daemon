#!/usr/bin/env python3
"""Feeds the playhead timecode of DaVinci Resolve to the Time Code Display of a JLCooper Eclipse.

The native JLCooperPanelDaemon watches a small text file, `timecode.txt`, next to its mapping file:

    ~/Library/Application Support/JLCooperPanelDaemon/timecode.txt

and shows what is in it ("HH:MM:SS:FF" or "HH:MM:SS;FF") on the panel's 7-segment display. Resolve does not
send the timecode to the panel helper by itself, so this script reads it through Resolve's scripting API (the
same API as any external Python script) and rewrites the file whenever the timecode changes. When Resolve
is not reachable, or there is no timeline, the file is removed and the display goes back to dashes.

While Resolve is *playing*, its scripting interface does not answer until playback stops. The script then keeps
the timecode running by itself at the timeline's frame rate (as if playing forward at normal speed) and snaps back
to the real value when Resolve answers again. Use --no-extrapolate to freeze the display during playback instead.

Needs: DaVinci Resolve with Preferences > System > General > "External scripting using" set to *Local*, and
the system Python 3 (stdlib only, no packages). Run it from a Terminal, or keep it running with launchd.

    python3 resolve_timecode_feeder.py [--file PATH] [--rate HZ] [--no-extrapolate]
"""
import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

API = os.environ.get("RESOLVE_SCRIPT_API",
                     "/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/Scripting")
LIB = os.environ.get("RESOLVE_SCRIPT_LIB",
                     "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Libraries/Fusion/fusionscript.so")
DEFAULT_FILE = os.path.expanduser("~/Library/Application Support/JLCooperPanelDaemon/timecode.txt")
TC_RE = re.compile(r"^(\d{1,3}):(\d{2}):(\d{2})([:;])(\d{2,3})$")

PENDING_AFTER = 0.15        # a Resolve call pending longer than this means "playing"
MAX_EXTRAPOLATE = 600.0     # seconds; give up (freeze) after this, in case Resolve is stuck for another reason

stop = False


def on_signal(signum, frame):
    global stop
    stop = True


def write_atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)


def remove(path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass


def load_api():
    os.environ.setdefault("RESOLVE_SCRIPT_API", API)
    os.environ.setdefault("RESOLVE_SCRIPT_LIB", LIB)
    sys.path.append(os.path.join(API, "Modules"))
    import DaVinciResolveScript as dvr
    return dvr


# ---- timecode <-> frame number (nominal fps, with SMPTE drop frame for 29.97 / 59.94)
def drop_per_minute(nominal):
    return 2 if nominal == 30 else 4 if nominal == 60 else 0


def tc_to_frames(tc, nominal):
    m = TC_RE.match(tc)
    h, mi, s, sep, f = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4), int(m.group(5))
    frames = ((h * 3600 + mi * 60 + s) * nominal) + f
    d = drop_per_minute(nominal)
    if sep == ";" and d:
        total_minutes = h * 60 + mi
        frames -= d * (total_minutes - total_minutes // 10)
    return frames


def frames_to_tc(frames, nominal, drop):
    d = drop_per_minute(nominal) if drop else 0
    if d:
        per_10min = nominal * 600 - d * 9
        per_min = nominal * 60 - d
        tens, rest = divmod(frames, per_10min)
        if rest > d:
            frames += d * 9 * tens + d * ((rest - d) // per_min)
        else:
            frames += d * 9 * tens
    f = frames % nominal
    s = (frames // nominal) % 60
    mi = (frames // (nominal * 60)) % 60
    h = frames // (nominal * 3600)
    return "%02d:%02d:%02d%s%02d" % (h, mi, s, ";" if d else ":", f)


def child_main(period):
    """Child process: asks Resolve for the timecode and prints one line per answer.
    A call can block for as long as Resolve is playing, which is why this is a separate process."""
    dvr = load_api()
    resolve = None
    timeline = None
    timeline_at = 0.0
    fps = 24.0
    ppid = os.getppid()
    while os.getppid() == ppid:
        started = time.monotonic()
        tc = None
        try:
            if resolve is None:
                resolve = dvr.scriptapp("Resolve")
            if resolve is not None:
                if timeline is None or started - timeline_at > 1.0:     # the current timeline can change
                    project = resolve.GetProjectManager().GetCurrentProject()
                    timeline = project.GetCurrentTimeline() if project else None
                    timeline_at = started
                    if timeline is not None:
                        try:
                            fps = float(timeline.GetSetting("timelineFrameRate"))
                        except Exception:
                            pass
                if timeline is not None:
                    tc = timeline.GetCurrentTimecode()
        except Exception:
            resolve = None
            timeline = None
        ok = isinstance(tc, str) and TC_RE.match(tc) is not None
        try:
            sys.stdout.write(("OK %s %s\n" % (tc, fps)) if ok else "NONE\n")
            sys.stdout.flush()
        except Exception:
            return
        time.sleep(period if resolve is not None else 2.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--file", default=DEFAULT_FILE, help="timecode file the daemon watches (default: %(default)s)")
    ap.add_argument("--rate", type=float, default=25.0, help="how often to ask Resolve, per second (default 25)")
    ap.add_argument("--no-extrapolate", action="store_true", help="freeze the display while Resolve is playing")
    ap.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    period = 1.0 / max(1.0, min(args.rate, 60.0))
    if args.child:
        child_main(period)
        return
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)
    os.makedirs(os.path.dirname(args.file), exist_ok=True)

    cmd = [sys.executable, os.path.abspath(__file__), "--child", "--rate", str(args.rate)]
    proc = None
    buf = b""
    tc, fps, answered = None, 24.0, 0.0          # last answer from Resolve and when it came
    last = None
    next_start = 0.0
    try:
        while not stop:
            now = time.monotonic()
            if proc is None and now >= next_start:
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
                buf = b""
            if proc is not None:
                r, _, _ = select.select([proc.stdout], [], [], 0.02)
                if r:
                    data = os.read(proc.stdout.fileno(), 4096)
                    if not data:                         # child ended: start a new one in a moment
                        proc.wait()
                        proc = None
                        next_start = time.monotonic() + 2.0
                        tc = None
                    else:
                        buf += data
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            parts = line.decode("ascii", "replace").split()
                            if parts[:1] == ["OK"] and len(parts) == 3:
                                tc, fps, answered = parts[1], float(parts[2]), time.monotonic()
                            else:
                                tc = None
            else:
                time.sleep(0.02)
            shown = tc
            if tc and not args.no_extrapolate:
                waited = time.monotonic() - (answered + period)        # how long the call in progress has been pending
                if PENDING_AFTER < waited < MAX_EXTRAPOLATE:
                    nominal = int(round(fps))
                    drop = TC_RE.match(tc).group(4) == ";"
                    shown = frames_to_tc(tc_to_frames(tc, nominal) + int(waited * fps), nominal, drop)
            if shown != last:
                if shown:
                    write_atomic(args.file, shown + "\n")
                else:
                    remove(args.file)
                last = shown
    finally:
        if proc is not None:
            proc.kill()
        remove(args.file)


if __name__ == "__main__":
    main()
