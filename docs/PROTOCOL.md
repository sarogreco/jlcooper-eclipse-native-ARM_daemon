# Observed protocol: JLCooper Eclipse ⇄ daemon ⇄ DaVinci Resolve

This is a description of the behaviour of the stock helper (`JLCooperPanelDaemon` 7.1, x86_64) as analysed for
this project, and of what this implementation does. It is **informal and may contain errors**; nothing here is
an official specification. All multi-byte values on the Resolve side are little-endian.

The daemon sits between the panel and Resolve:

```
 Eclipse panel  ── TCP :23, F0 15 25 … F7 frames ──  daemon  ── unix socket, "Steinway" packets ──  DaVinci Resolve
```

Resolve 21.1 launches it as `JLCooperPanelDaemon -s <fd> -l <panel.log> <host>:23`, handing it an already
connected socket. Without `-s` the daemon listens on `/var/tmp/davinci_socket` (backlog 1).

## Resolve side ("Steinway" packets)

Header, 12 bytes: `u16 dest | u16 cmd | u16 target | u16 reserved(0) | u32 payload_length`, followed by the payload.
Panel selector `panelbits(p)`: p=1 → 0x2000, p=2 → 0x1000, p=0 → 0, other → 0x4000 (OR-ed into `target`).

### Daemon → Resolve (`dest` is always 0)

| cmd | meaning | target | payload |
|---|---|---|---|
| 3 | key press | ctrl \| 0x200 \| panelbits | u16 = 1 (no release is ever sent) |
| 4 | pot / encoder delta | ctrl \| 0x100 \| panelbits | i16 delta |
| 4 | trackball ring | n \| 0x310 \| panelbits(2) | i16 delta |
| 4 | jog wheel | 0x400 | i16 delta |
| 5 | trackball X/Y | n \| 0x300 \| panelbits(2) | i16 {a, b}; vertical: {0,-v}, horizontal: {-v,0} |
| 6 | pot reset | ctrl \| 0x100 \| panelbits | u16 = 1 |
| 10 | status reply | 0 | u16 absent (1 = panel not connected), 0, u16 number of LCD images received, 0 |

Id decoding: key/pot ids carry the panel in bits 8..11 (0x100 → panel 1, 0x200 → panel 2, else 0) and the
control in the low byte. Keys whose id has bits 12..15 = 1 or 2 are preceded by key 0x340 or 0x3a5
respectively.

### Resolve → daemon

`dest` 0/1/2/4 → panel 0/2/1/all. Implemented commands:

| cmd | meaning |
|---|---|
| 1 | LCD image download: remembers the u16 image number (requires payload ≥ 0x22 and ≥ u32(payload+8)+0x22); the image itself is not displayed |
| 10 | status request (answered with cmd 10 above) |
| 13 | forget the stored image numbers |
| 14 | no-op |

Everything else is logged and ignored.

## Panel side (TCP port 23): frames `F0 15 25 <payload> F7`

**To the panel**

* `03` — request ID. `02 02 02 00 00` — set mode (sent first, one second after the TCP connect).
* LED `55 03 <id'> <!on>` for ids 0..13 (mapping 8↔9, 10 → 0x0d, 11 → 0x0c, 12 → 0x11, 13 → 0x10);
  LED `42 <led-0x17> <on>` for ids 0x17..0x53; `4B <b>` for ids 0x15/0x16 (sticky OR); `BF 6B <b>` shuttle LEDs.
* `4A <hi> <lo>`; 7-segment text `49 <8 segment bytes>`.
* LCD: `55 01 <disp<<6 | col | line<<5> <len ≤ 0x18> <chars>` for displays 0/1, and
  `43 <line<<6 + col> <len> <chars>` for display 2.

**From the panel** (`F0 15 25 <cmd> <b4> <b5> F7`)

* `55`: b4 ≤ 0x0e → button(b4, pressed = b5==0); 0x26..0x2b → button(b4−0x17); 0x20..0x25 → rotor(b4−0x1f);
  0x2d..0x35 → rotor(b4−0x26); rotor value = b5 as 7-bit signed.
* `42`: button(b4+0x17, pressed = b5==1). `47`: jog. `48`: shuttle. `46`: encoders 7..11 (rotor b4+0x12).
* `03`, `24`, `41`, `45`: logged only.

## Behaviour of the controller

* Button 0x2b is SHIFT. Buttons are dispatched on press only: shifted table (17 entries) or plain table (59
  entries) → knob reset → mode switch (buttons 0..7) → plus/minus (button 0x49).
* Each mode selects the knob → pot assignment for rotors 1..3 (table A) and 4..6 (table B) and the LCD labels.
* Shuttle is emulated with a 5 ms timer: every (|v| + 12) ticks one jog step of ±1 is sent.
* On connect: all LEDs off, set mode, LED of the current mode on, 7-segment `--------`, `4A 00 00`, the two
  information lines on LCD 2.
