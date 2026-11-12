# JLCooper Eclipse panel daemon for DaVinci Resolve — native Apple Silicon build (unofficial)

*[Leggi in italiano](README.it.md)*

DaVinci Resolve talks to a JLCooper **Eclipse** control panel (Ethernet) through a small helper program,
`JLCooperPanelDaemon`, that lives inside the Resolve app. That helper is **x86_64 only**: on Apple Silicon it
runs through Rosetta 2, and Apple is phasing Rosetta out of future macOS versions. Without it the panel is
simply not detected ("bad CPU type in executable").

This project is a drop-in **native replacement** for that helper. It speaks the same two protocols (the
panel's TCP protocol and Resolve's local socket protocol), takes the same command-line options, and builds
as a normal arm64 (or x86_64) macOS executable.

> **Unofficial.** Not made, endorsed or supported by Blackmagic Design or JLCooper. Replacing a file inside
> the Resolve application is your decision and your risk — see [Legal notice](#legal-notice) and the
> [license](LICENSE). If Blackmagic Design ships a native build of its own helper, use that instead.

## Status

Tested by the maintainer on one setup:

| | |
|---|---|
| Panel | JLCooper Eclipse, Ethernet (panel ID reply `03 0b 18 00 00 02`) |
| Resolve | DaVinci Resolve Studio 21.1 |
| macOS | 27.2, Apple Silicon |

With Resolve running the replacement (no Rosetta) everything the maintainer tried worked: trackballs and
rings, knobs, jog and shuttle, buttons, and the mode keys with their LEDs and LCD labels. The optional
[mapping file](docs/MAPPING.md) has also been used on that panel: with it, SHIFT + knobs 1–3 of the Offset page
drive Color Temp, Tint and Highlights in Resolve (Shadows and, on knob 5, Color Boost were added afterwards) while Custom Curves
is open (whichever palette is selected on the left of the Color page; otherwise they give anomalies), see the
[notes](docs/MAPPING.md#these-ids-need-custom-curves-to-be-open).

The three trackball lights and the Time Code display (which the stock helper never drives, nor the key backlight) have been made to
work on that panel too, see [docs/TIMECODE.md](docs/TIMECODE.md).

**Not verified on real hardware:** the drop-frame mark of the Time Code display (could not be lit), encoders 7–11,
shifted key combinations other than the ones in the tests, other Eclipse hardware/firmware revisions, other
Resolve or macOS versions. The behaviour is covered by 157 automated black-box checks (fake panel + fake Resolve) and
sanitizer/fuzz runs, but a simulator only proves that the code does what its author *thinks* the original
does. **Test reports are very welcome** — see [Contributing](#contributing).

## Download a ready-made build (no Xcode needed)

Apple Silicon only. Get the zip from the [latest release](https://github.com/sarogreco/jlcooper-eclipse-native-ARM_daemon/releases/latest).
It is built by GitHub Actions from the source in this repository and tested with the same black-box tests
before it is published; it is ad-hoc signed, not notarised. Unzip it, open a Terminal in the unzipped folder and
check it before use:

```sh
xattr -dr com.apple.quarantine .      # drop the "downloaded from the internet" flag
file JLCooperPanelDaemon              # must say: Mach-O 64-bit executable arm64
shasum -a 256 JLCooperPanelDaemon     # must match the line in SHA256SUMS on the release page
```

Then continue with [Install into Resolve](#install-into-resolve), using this file as the binary. You are about to
run it with `sudo` inside the Resolve app: if you would rather not trust a prebuilt file, build it yourself below.

## Build

You need the Xcode Command Line Tools (`xcode-select --install`).

Download this repository (green *Code* button → *Download ZIP*, or `git clone`), open a Terminal in its
folder and run:

```sh
make
file build/JLCooperPanelDaemon      # Mach-O 64-bit executable arm64   (x86_64 on an Intel Mac)
```

or without `make`:

```sh
clang++ -std=c++17 -O2 -Wall -o JLCooperPanelDaemon src/JLCooperPanelDaemon.cpp
```

## Try it without Resolve (changes nothing)

Quit Resolve and stop the stock helper first — the panel accepts a single connection:

```sh
pkill -x JLCooperPanelDaemon
build/JLCooperPanelDaemon -V 192.168.254.101:23      # use your panel's address
```

After about a second the panel should turn all LEDs off, light the mode‑0 LED, show labels on its LCDs and
`--------` on the 7‑segment display. Buttons and knobs print log lines (`Received button 1 press`,
`Rotor [1] has been turned by [5]`, …). `Ctrl‑C` quits. Your Mac and the panel need different addresses on the
same subnet (macOS warns if another device uses the Mac's IP).

## Install into Resolve

Resolve starts the helper itself from inside its application bundle. Keep a backup of the original, then
copy the new binary over it and re-sign it ad hoc:

```sh
APP="/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Applications"

# 1. backup of the original — only if none exists yet, so running this twice can never
#    overwrite the original with the new binary
[ -e ~/JLCooperPanelDaemon.original ] || sudo cp -p "$APP/JLCooperPanelDaemon" ~/JLCooperPanelDaemon.original

# 2. install and sign (BIN: the file you built, or the one from the release zip)
BIN=build/JLCooperPanelDaemon
sudo cp "$BIN" "$APP/JLCooperPanelDaemon"
sudo codesign --force --sign - "$APP/JLCooperPanelDaemon"
```

macOS may ask for the *App Management* permission for Terminal (System Settings → Privacy & Security).
Start Resolve and check which helper is running:

```sh
ps -axo pid,ppid,args | grep -i "[J]LCooper"
file "$APP/JLCooperPanelDaemon"
```

Resolve 21.1 launches it as `JLCooperPanelDaemon -s <fd> -l <…/logs/panel.log> <panel-ip>:23`. In Activity
Monitor the *Kind* column shows **Apple** for a native process and **Intel** for one running under Rosetta.

**Roll back** at any time:

```sh
sudo cp -p ~/JLCooperPanelDaemon.original "$APP/JLCooperPanelDaemon"      # the original needs Rosetta
```

**Resolve updates replace the helper** with the stock x86_64 one. After an update, run the `file` command
above; if it says `x86_64`, repeat step 2 (keep the built binary somewhere safe).

## Troubleshooting

* Nothing happens in Resolve: close Resolve, run the daemon by hand with `-V -l ~/jlc.log <ip>:23` as above and
  watch the panel and the log.
* `panel.log` of a Resolve session is normally empty with this build: events are logged only with `-V`, which
  Resolve does not pass. That is expected, not an error.
* To get a verbose log *inside* a Resolve session you can swap in a tiny wrapper (not verified with Resolve,
  but `exec` keeps the file descriptor Resolve passes with `-s`):

  ```sh
  cd "$APP"
  sudo mv JLCooperPanelDaemon JLCooperPanelDaemon.native
  printf '#!/bin/sh\nexec "$(dirname "$0")/JLCooperPanelDaemon.native" -V "$@"\n' | sudo tee JLCooperPanelDaemon >/dev/null
  sudo chmod +x JLCooperPanelDaemon
  ```

  Restore with `sudo mv JLCooperPanelDaemon.native JLCooperPanelDaemon`.

## Options

Same as the original helper: `JLCooperPanelDaemon [-hvVdr] [-s fd] [-l logfile] [hostname[:port]]`

| option | meaning |
|---|---|
| `-s fd` | use the already-connected socket `fd` to talk to Resolve (how Resolve starts it) |
| `-l file` | write errors/logs to `file` instead of stderr |
| `-c file` | optional [mapping file](docs/MAPPING.md) (default `~/Library/Application Support/JLCooperPanelDaemon/mapping.conf`) |
| `-V` | verbose log |
| `-d` | show the last button on the panel's 7-segment display |
| `-r` | accepted for compatibility; this build always keeps retrying the panel connection |
| `-v`, `-h` | version, help |

Without `-s` the daemon listens on `/var/tmp/davinci_socket` like the original. Default panel address:
`192.168.254.101:23`.

## Differences from the original helper

* Reconnects to the panel by itself if the connection drops or the panel is off at start-up.
* Does not block during the one-second pause after connecting.
* Validates the lengths of packets coming from Resolve.
* Debug output on the LCDs (`-d`) is reduced to the 7-segment indicator.
* `-v` prints the same version line (`… version 7.1`) but a different second line.
* **Extra, optional:** a [mapping file](docs/MAPPING.md) can give the knobs a SHIFT layer (for example Color Temp,
  Tint and Highlights, which the stock mapping does not use) and has a probe mode for finding Resolve control ids.
  Without the file the behaviour is the stock one.
* **Extra, optional:** the three trackball lights can be lit in a colour of your choice (`ballcolors` in the
  mapping file), the keys can be backlit for a dark room (`keylights`), and the Time Code display can show Resolve's playhead timecode, fed through a small text file by
  a helper script that can run at login. See [docs/TIMECODE.md](docs/TIMECODE.md).

## How it works

The panel speaks `F0 15 25 … F7` frames over TCP port 23; Resolve speaks 12-byte-header "Steinway" packets
over a local unix socket. The daemon translates between them and runs the panel's mode/shift/knob logic and
the shuttle emulation. Details in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Tests

```sh
make test        # fake panel + fake Resolve over a socketpair (like Resolve's -s launch)
make test-uds    # the same plus the listening-socket mode on /var/tmp/davinci_socket
make asan        # sanitizer build, tests, a fuzz run and a fuzz run of the mapping-file parser
```

The tests are timing based; run them on an idle machine. They never touch `/var/tmp/davinci_socket` if
something (for example a running Resolve) is listening on it.

## Contributing

Reports from other panels and Resolve/macOS versions are the most useful thing right now — open an issue
with the *Test report* template and attach a `-V` log. Pull requests are welcome; please keep
`make test` green and do not add files extracted from Blackmagic Design or JLCooper software.

## Support

The software is free (MIT). If it saved you time, you can [buy me a coffee](https://buymeacoffee.com/sarogreco) —
entirely optional, and it does not entitle anyone to support.

## Legal notice

* "DaVinci Resolve", "Blackmagic Design" and "JLCooper" are trademarks of their respective owners; they are
  used here only to say which products this software works with.
* This repository contains no Blackmagic Design or JLCooper software, firmware or documentation. The daemon
  is an independent reimplementation written from an analysis of the original helper's observable behaviour
  and protocols, for the purpose of interoperability. The lookup tables in the source (LED ids, button and
  knob maps, LCD labels) describe what the original does and were taken from it.
* Whether modifying files inside the Resolve application is permitted by your license agreement, and what
  the law of your country says about interoperability work, is for you to check. Modifying the application
  may affect support.
* If you represent Blackmagic Design or JLCooper and have a concern about this project, please open an issue.

## Credits

Maintained by Sebastiano Greco, who ran the real-hardware tests and directed the work. The protocol analysis,
the implementation and the test suite were developed with the help of Claude (Anthropic).

## License

[MIT](LICENSE). No warranty.
