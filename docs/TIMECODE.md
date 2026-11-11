# Trackball lights, key lights and Time Code display (optional)

Things the stock helper never does on the Eclipse, all optional and off until you set them up:

1. the **three trackball lights** (Lift, Gamma, Gain) — a line in the [mapping file](MAPPING.md);
2. the **key lights**, to find the keys in a dark room — another line in the mapping file;
3. the **Time Code display** (the 7-segment display) showing the playhead timecode of Resolve — a small
   helper script plus a text file.

Verified on one setup (Eclipse, Resolve Studio 21.2, macOS, Apple Silicon). Reports from other setups are welcome.

## Trackball lights

Each ball has two small LEDs, a green one and a blue one; both on looks red. Add one line to `mapping.conf`:

```
ballcolors <left> <centre> <right>       # each: off | green | blue | red
```

For example the colours of the Eclipse CX product photos (left red, centre green, right blue):

```
ballcolors red green blue
```

* The lights are set when the panel connects, when the file is saved, and after a panel reconnect. They are
  switched off when the daemon quits. Without the line they are left alone (off).
* Removing the line (or the file) switches them off.
* The colour is fixed: it does not follow Resolve's state (which palette, which wheel).

Ids, for the curious: left ball `55 03 <id'> <!on>` with ids 10 (green) and 11 (blue), centre 14 and 15, right
18 and 19 — see [PROTOCOL.md](PROTOCOL.md).

## Key lights

The stock helper lights only the key of the active mode; Resolve never drives the other key LEDs. In a dark
grading room that makes the keys hard to find. One line in `mapping.conf` lights them:

```
keylights on        # every key LED on, except the 8 mode keys, which keep showing the active mode
keylights all       # the mode keys too (then the active mode is no longer visible on them, only on the LCDs)
keylights off       # the stock behaviour (also what you get without the line)
```

* The LEDs are on or off: no brightness control was found on the panel.
* The shuttle direction LEDs keep their normal job and are not touched.
* The lights are set when the panel connects, when the file is saved, and after a panel reconnect; they are
  switched off when the daemon quits.
* The list of LEDs is the one the original helper clears at start-up (46 LEDs). If a key you expect to light
  stays dark, open an issue with its name.
* The **keys around the shuttle ring** stay dark on the tested panel: no LED command tried lit them (every `42`
  and `55 03` id up to 0x7f and every `4B` bit), and they are dark in the manufacturer's product photos too, so
  they most likely have no LED.

## Time Code display

Resolve does not send the timecode to the panel helper, so the display shows `--------` with the stock helper.
This daemon can show any timecode it finds in a small text file:

    ~/Library/Application Support/JLCooperPanelDaemon/timecode.txt

(the same folder as `mapping.conf`; if you use `-c <file>` or `JLCD_CONFIG` it is `timecode.txt` next to that file).

* One line, `HH:MM:SS:FF`, or with `;` before the frames for drop frame (`HH:MM:SS;FF`). `.` also works as
  separator. Each field shows two digits.
* The file is looked at about 25 times a second while a timecode is shown (5 times a second otherwise), so the
  display follows within a frame or two.
* **Removing the file puts the dashes back.** A half-written or garbled line is ignored (the display keeps what
  it shows), but write the file atomically (write a temporary file, rename it over `timecode.txt`) if you
  write it yourself.
* [Probe mode](MAPPING.md#finding-the-id-of-a-control-probe-mode) owns the display while it is on; the
  timecode comes back when it is off.

Anything can write that file — a shell loop, another application. The ready-made writer for Resolve is
`tools/resolve_timecode_feeder.py`.

### The feeder script for Resolve

It reads the playhead of the current timeline through Resolve's own scripting API and rewrites `timecode.txt`
when it changes. It needs:

* Resolve (Studio) with **Preferences ▸ System ▸ General ▸ External scripting using ▸ Local**;
* the system Python 3 (`/usr/bin/python3`, installed with the Xcode Command Line Tools). Standard library only.

Try it by hand first — in a Terminal, with Resolve open on a project with a timeline:

```sh
python3 tools/resolve_timecode_feeder.py
```

The display should show the playhead timecode; `Ctrl-C` stops it and the dashes come back. Options:

| | |
|---|---|
| `--file PATH` | the timecode file (default as above) |
| `--rate HZ` | how many times per second to ask Resolve (default 25, 1–60) |
| `--no-extrapolate` | freeze the display during playback instead of counting (see below) |

To have it run by itself — start at login, restart if it stops — install it as a per-user LaunchAgent (no sudo, nothing
outside your home folder):

```sh
sh tools/timecode-feeder.sh install      # also: status, uninstall
```

It copies the script to `~/Library/Application Support/JLCooperPanelDaemon/`, writes
`~/Library/LaunchAgents/com.github.sarogreco.jlcooper-timecode-feeder.plist`, and logs to
`~/Library/Logs/JLCooperTimecodeFeeder.log`. While Resolve is closed, or there is no timeline, the file is
absent and the panel shows dashes; start Resolve and the timecode appears. `uninstall` removes everything.

### How it behaves

* **Stopped, scrubbing, jog, stepping:** follows the real playhead, about 25 times a second. Each question costs
  Resolve roughly a millisecond or two.
* **While Resolve is playing**, its scripting interface does not answer until playback stops (a call just waits).
  The feeder notices a call pending for more than a moment and then *counts by itself* at the timeline frame
  rate (drop frame handled for 29.97 and 59.94), and snaps to the real timecode as soon as Resolve answers
  again, when you stop. So during play the display is a good estimate, not a reading. Whatever the playback
  speed or direction, the estimate assumes forward at normal speed: in reverse or at JKL speeds the display
  runs at the wrong rate until you stop. Use `--no-extrapolate` if you would rather have it freeze.
* The count gives up after ten minutes of Resolve not answering (the display freezes) in case Resolve is stuck
  rather than playing.

### Limits

* The **DF mark** (drop-frame indicator) next to the digits could not be switched on. The daemon accepts
  `HH:MM:SS;FF` and shows the digits, but not the mark. Tried on one panel without visible effect: bit 7 of the
  digit bytes, the `4A` command, and every `42`/`55` LED id.
* The display always shows hours, minutes, seconds and frames; the layout is not configurable.
* Only the current timeline's playhead is available; the source viewer's timecode is not.
