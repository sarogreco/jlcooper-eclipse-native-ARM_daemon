#!/bin/sh
# Installs / removes the Resolve timecode feeder as a per-user LaunchAgent (starts at login, restarts if it stops).
#
#   sh tools/timecode-feeder.sh install     copy the script and start it now and at every login
#   sh tools/timecode-feeder.sh status      is it running, and what does it write
#   sh tools/timecode-feeder.sh uninstall   stop it and remove the LaunchAgent and the copied script
#
# No sudo needed, nothing outside your home folder is touched. See docs/TIMECODE.md.
set -eu

LABEL="com.github.sarogreco.jlcooper-timecode-feeder"
HERE=$(cd "$(dirname "$0")" && pwd)
SUPPORT="$HOME/Library/Application Support/JLCooperPanelDaemon"
DEST="$SUPPORT/resolve_timecode_feeder.py"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/JLCooperTimecodeFeeder.log"
PYTHON="${PYTHON:-/usr/bin/python3}"
LAUNCHCTL="${LAUNCHCTL:-launchctl}"
DOMAIN="gui/$(id -u)"

stop_all() {
    "$LAUNCHCTL" bootout "$DOMAIN/$LABEL" >/dev/null 2>&1 || true
    pkill -f resolve_timecode_feeder.py >/dev/null 2>&1 || true     # also one started by hand in a Terminal
}

case "${1:-}" in
install)
    [ -f "$HERE/resolve_timecode_feeder.py" ] || { echo "resolve_timecode_feeder.py not found next to this script" >&2; exit 1; }
    [ -x "$PYTHON" ] || { echo "$PYTHON not found (install the Xcode Command Line Tools: xcode-select --install)" >&2; exit 1; }
    mkdir -p "$SUPPORT" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    cp "$HERE/resolve_timecode_feeder.py" "$DEST"
    chmod 755 "$DEST"
    cat > "$PLIST" <<PLISTEOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$PYTHON</string>
        <string>$DEST</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ThrottleInterval</key>
    <integer>10</integer>
    <key>StandardOutPath</key>
    <string>$LOG</string>
    <key>StandardErrorPath</key>
    <string>$LOG</string>
</dict>
</plist>
PLISTEOF
    if command -v plutil >/dev/null 2>&1; then plutil -lint "$PLIST" >/dev/null; fi
    stop_all
    "$LAUNCHCTL" bootstrap "$DOMAIN" "$PLIST"
    echo "Installed. The timecode feeder is running and will start at every login."
    echo "  script: $DEST"
    echo "  log:    $LOG"
    echo "Resolve needs Preferences > System > General > External scripting using: Local."
    ;;
status)
    if "$LAUNCHCTL" print "$DOMAIN/$LABEL" >/dev/null 2>&1; then echo "LaunchAgent: loaded ($LABEL)"; else echo "LaunchAgent: not loaded"; fi
    if pgrep -f resolve_timecode_feeder.py >/dev/null 2>&1; then echo "Process: running"; else echo "Process: not running"; fi
    if [ -f "$SUPPORT/timecode.txt" ]; then echo "timecode.txt: $(cat "$SUPPORT/timecode.txt")"; else echo "timecode.txt: absent (display shows dashes: Resolve closed or no timeline)"; fi
    ;;
uninstall)
    stop_all
    rm -f "$PLIST" "$DEST" "$SUPPORT/timecode.txt"
    echo "Removed. The panel display shows dashes again."
    ;;
*)
    echo "usage: sh $0 install | status | uninstall" >&2
    exit 2
    ;;
esac
