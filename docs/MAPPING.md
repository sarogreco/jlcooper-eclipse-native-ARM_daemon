# Custom mapping (optional)

The daemon can read a small text file that

* adds a **SHIFT layer** to the knobs — hold SHIFT and a knob drives a different Resolve control, with
  its own labels on the LCDs while SHIFT is held — and
* has a **probe mode** for finding the id of a Resolve control that the stock mapping does not use, and
* can light the three **trackball lights** (`ballcolors`) and the **key backlight** (`keylights`; the Time Code
  display has its own file) — see [TIMECODE.md](TIMECODE.md).

Without the file the daemon behaves exactly like the stock helper.

## The file

Looked for, in this order: the `-c <file>` option, the `JLCD_CONFIG` environment variable, and

    ~/Library/Application Support/JLCooperPanelDaemon/mapping.conf

(`~` is `$HOME`; Resolve starts the helper without `HOME` in its environment, so the daemon then asks the
system for the home directory.) At start-up the daemon always writes one line to its log saying which file it
looks for and whether it found it — in a Resolve session that is `panel.log`, in
`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`.

The file is **re-read automatically** within a fraction of a second of being saved: no need to restart the daemon or
Resolve. Every (re)load is reported in the log (`panel.log` when Resolve starts the daemon), together with any
line it could not understand; bad lines are skipped and the rest still applies. Deleting the file restores
the built-in mapping.

```
# comment
shiftpot   <mode 0-7> <knob 1-6> <pot>              # knob drives <pot> while SHIFT is held
shiftlabel <mode 0-7> <knob 1-6> <upper>|<lower>    # LCD label while SHIFT is held (max 8 characters each)
probe      on|off                                   # probe mode, see below
probestart <pot>                                    # id the probe starts at
send       <pot> <delta>                            # one-shot command for scripts, see below
reset      <pot>                                    # one-shot command for scripts, see below
ballcolors <left> <centre> <right>                  # trackball lights: off | green | blue | red each
keylights  off|on|all                               # key backlight, see TIMECODE.md
```

* **mode** is the mode key as numbered in the log (`Received button N`): 0 to 7.
* **knob** 1-3 are the knobs of the first display (left to right), 4-6 those of the second display.
* **pot** is a control id of Resolve, written `<panel>/<control>` (panel 0, 1 or 2; control 0-255, decimal or
  `0x..`) or as the raw number used inside the daemon (`0x25b` = panel 2, control 0x5b = `2/91`).
* A knob without `shiftpot` keeps its normal function while SHIFT is held. SHIFT + the push button of a knob
  that has a SHIFT function resets *that* function.
* `ballcolors` and `keylights` are described in [TIMECODE.md](TIMECODE.md#trackball-lights); for example
  `ballcolors red green blue` and `keylights on`.
* `shiftlabel` lines are optional; labels are centred in the knob's 8-character column of the display
  (upper line = name, lower line = qualifier, like "Red" / "Offset").

## Control ids used by the stock mapping

These are the ids the stock helper sends, per mode key and knob (panel 0 is written as `0x3..` inside the
daemon). Ids that are not in this table may or may not do something in Resolve — that has to be tried.

| mode | knob | pot | raw | LCD label |
|---|---|---|---|---|
| 0 | 1 | `0/0` | `0x300` | Pan Image |
| 0 | 2 | `0/1` | `0x301` | Tilt Image |
| 0 | 3 | `0/2` | `0x302` | Zoom Image |
| 0 | 4 | `0/3` | `0x303` | Rotate Image |
| 0 | 5 | `0/4` | `0x304` | HSize |
| 0 | 6 | `0/5` | `0x305` | VSize |
| 1 | 1 | `2/64` | `0x240` | Lift |
| 1 | 2 | `2/48` | `0x230` | Gamma |
| 1 | 3 | `2/32` | `0x220` | Gain |
| 1 | 4 | `2/33` | `0x221` | Sat |
| 1 | 5 | `2/49` | `0x231` | Hue |
| 1 | 6 | `2/65` | `0x241` | Lum Mix |
| 2 | 1 | `2/91` | `0x25b` | Red Offset |
| 2 | 2 | `2/92` | `0x25c` | Green Offset |
| 2 | 3 | `2/93` | `0x25d` | Blue Offset |
| 2 | 4 | `2/90` | `0x25a` | Master Offset |
| 2 | 5 | `2/81` | `0x251` | Contrast |
| 2 | 6 | `2/97` | `0x261` | Pivot |
| 3 | 1 | `1/0` | `0x100` | Zoom Window |
| 3 | 2 | `1/1` | `0x101` | Aspect |
| 3 | 3 | `1/3` | `0x103` | Rotate Window |
| 3 | 4 | `1/2` | `0x102` | Softness |
| 3 | 5 | `1/4` | `0x104` | Pan Window |
| 3 | 6 | `1/5` | `0x105` | Tilt Window |
| 4 | 1 | `1/0` | `0x100` | Zoom Window |
| 4 | 2 | `1/1` | `0x101` | Aspect |
| 4 | 3 | `1/6` | `0x106` | Inside Softness |
| 4 | 4 | `1/7` | `0x107` | Outside Softness |
| 4 | 5 | `0/6` | `0x306` | Blur Radius |
| 4 | 6 | `0/7` | `0x307` | H/V Ratio |
| 5 | 1 | `0/8` | `0x308` | Hue Center |
| 5 | 2 | `0/9` | `0x309` | Hue Width |
| 5 | 3 | `0/10` | `0x30a` | Hue Soft |
| 5 | 4 | `0/11` | `0x30b` | Hue Sym |
| 5 | 5 | `0/211` | `0x3d3` | White Clip |
| 5 | 6 | `0/209` | `0x3d1` | Clean White |
| 6 | 1 | `0/16` | `0x310` | Lum LowClip |
| 6 | 2 | `0/17` | `0x311` | Lum LowSoft |
| 6 | 3 | `0/18` | `0x312` | Lum HiSoft |
| 6 | 4 | `0/19` | `0x313` | Lum HiClip |
| 6 | 5 | `0/210` | `0x3d2` | Black Clip |
| 6 | 6 | `0/208` | `0x3d0` | Clean Black |
| 7 | 1 | `0/12` | `0x30c` | Sat LowClip |
| 7 | 2 | `0/13` | `0x30d` | Sat LowSoft |
| 7 | 3 | `0/14` | `0x30e` | Sat HiSoft |
| 7 | 4 | `0/15` | `0x30f` | Sat HiClip |
| 7 | 5 | `0/213` | `0x3d5` | Blur Radius |
| 7 | 6 | `0/212` | `0x3d4` | In/Out Ratio |

## Control ids found by scanning (panel 2, one setup)

Found on the maintainer's setup (Eclipse, DaVinci Resolve Studio 21.2, Color page, *Rec.709 colour managed*) by
sending a small turn to every control id and looking at what changed (see [the scan](#scanning-with-a-script)).
Resolve decides what an id does, so other Resolve versions or panel types may differ. "Confirmed" = used from the
real panel; "inferred" = identified from how the picture changes, not yet confirmed on a panel.

| id | does | notes |
|---|---|---|
| `2/8` | **Color Temp** | `+` warms (R up, B down). Confirmed. |
| `2/9` | **Tint** | `+` towards magenta (R and B up, G down). Confirmed. |
| `2/24` | **Highlights** | effect grows with the brightness of the pixel. Confirmed. |
| `2/25` | **Shadows** | effect strongest on dark tones, fading towards the bright ones. Inferred. |
| `2/10` | Contrast | identical to `2/81`. |
| `2/0`…`2/3` | Offset: master, R, G, B | identical to `2/90`…`2/93`. |
| `2/26` | **Color Boost** | Confirmed from the real panel: the Color Boost field moves. Like Temp, Tint, Highlights and Shadows it needs Custom Curves to be open, see below. |
| `2/27` | not named | a very small contrast-like change; sits next to Highlights/Shadows/Color Boost and may be Mid/Detail; unconfirmed. |
| `2/32`, `2/48`, `2/64` | Gain, Gamma, Lift (rings) | as in the table above. |
| `2/33`, `2/49`, `2/65` | Saturation, Hue, Lum Mix | as in the table above. |
| `2/4`–`2/7`, `2/16`–`2/21`, `2/128`–`2/133` | Custom Curves | leave a "Custom Curves" entry in the node. With a Hue curve open, `2/0`–`2/11` and `2/16`–`2/21` are its colour points instead, see [Hue curves](#hue-curves-the-six-colour-points-of-hue-vs-hue-hue-vs-sat-and-hue-vs-lum). |
| `2/208`–`2/246`, and the same range on panels 0 and 1 | RGB Mixer, Blur/Sharpen/Mist, Noise Reduction, Motion Blur, Sizing | see the warning below. |

Nothing else between `2/11` and `2/205` did anything visible, on any of the three panels.

### These ids need Custom Curves to be open

Only the ids for the stock functions (Lift, Gamma, Gain, Saturation, Hue, Lum Mix, Contrast, Offset) work the
same whatever is open in Resolve. The others, `2/8`, `2/9`, `2/24`, `2/25` and `2/26` (Color Temp, Tint,
Highlights, Shadows, Color Boost), work as described only **while Custom Curves is open**. Using them on the real
panel the maintainer found that this is what counts, **whichever palette is selected on the left of the Color
page**; without Custom Curves open they give anomalies.

The first scan had described the same dependence as "depends on the open palette". What it saw, kept here as a
list of what can go wrong (not as a list of supported palettes):

| open palette | what the ids did in the scan |
|---|---|
| Primaries – Color Wheels, Curves, Color Slice, Color Warper | work as described above |
| Qualifier | move a Qualifier luminance parameter instead |
| Sizing | distort the picture (they move Sizing parameters) |
| Window, Tracker, Magic Mask, Blur, Key | do nothing |

With Window open a second scan found **no** id for these four functions on any panel (0, 1 or 2, controls 0–205), so
they cannot be had there with this panel. The daemon is not told what is open in Resolve, so it cannot block
the ids when Custom Curves is not open: do not turn these knobs then, and never while Qualifier or Sizing is open. Forcing a colour
palette from the daemon would move you out of Window (and hide the window outline), so it was not done.

A scan like this can also not see anything that needs a window or a qualifier to be switched on first.

Scale, as a feeling for the numbers: a delta of 30 moves Saturation by 0.06 and an Offset channel by 0.011; a delta
of 300 on Color Temp shifts a mid-grey pixel by about +11 (R) and −17 (B) in 8-bit code values.

**Warning for scans.** The ids in the 206–255 range change the clip's *Sizing* (zoom, rotation, perspective) and
blur settings, and turning them back by the opposite amount does **not** bring the picture back. Some curve ids
also leave a "Custom Curves" entry behind (`reset <pot>` clears most of them; *Reset All Grades* clears the rest).
Skip 206–255 when scanning, and keep a throwaway timeline: if the picture ends up distorted, make a fresh timeline
from the same clip. Scanning with the Window or Qualifier palette open also changes window and qualifier settings
and may reset parts of the grade (Offset, Saturation, Contrast, the qualifier), so do it on a timeline you do not
care about, and do not use `reset` on ids you do not know.

## Hue curves: the six colour points of Hue vs Hue, Hue vs Sat and Hue vs Lum

With one of the Hue curves open in the Curves palette, a few of the ids above stop meaning "Offset" or "Custom
Curves" and move the six fixed colour points of the open curve instead. Found on the maintainer's setup (Eclipse,
DaVinci Resolve Studio 21.2); the ids are addressed absolutely, so SHIFT + knob gives the panel direct control of
a point that otherwise needs the mouse:

| colour (Input Hue) | Hue vs Hue | Hue vs Sat | Hue vs Lum |
|---|---|---|---|
| Red (256) | `2/0` | `2/16` | `2/6` |
| Yellow (316) | `2/1` | `2/17` | `2/7` |
| Green (16) | `2/2` | `2/18` | `2/8` |
| Cyan (76) | `2/3` | `2/19` | `2/9` |
| Blue (136) | `2/4` | `2/20` | `2/10` |
| Magenta (196) | `2/5` | `2/21` | `2/11` |

What to expect:

* **Turning one of these also selects the curve type.** Sending an id from the Hue vs Sat column while Hue vs Hue
  is open switches Resolve to Hue vs Sat (and the same for Hue vs Lum), so the three columns do not need
  the matching curve to be open first — any Hue-family curve will do.
* **They do nothing from Custom.** With Curves – Custom open these ids have no effect (there they are the
  Custom Curves ids listed above). The Curves palette remembers the sub-mode it was last in, and **no panel key
  selects a sub-mode**: about 600 key ids were tried, none switches between Custom and the Hue curves. Open the
  curve you want with the mouse once; after that the panel can drive it.
* Other palettes (Color Slice, Color Warper, …) were not tested for these ids.
* They overlap with ids used for other things (for example `2/8` is Color Temp in the palette table above and Hue
  vs Lum Green here). Which one you get depends on the palette that is open, so the SHIFT layers in the examples at
  the end are separate pages: Page 3 keeps Temp, Tint, Highlights, Shadows and Color Boost (all five need Custom Curves open), and
  the Hue curves get pages of their own.

## Finding the id of a control (probe mode)

Resolve decides what an id does; the daemon cannot know. Probe mode lets you walk through ids while watching
Resolve. **Use a throwaway clip/timeline** — an id you try may change a parameter, and you will be turning a knob.

1. Open the Color page with the *Primaries – Color Wheels* panel visible (Temp, Tint, Contrast, Pivot, … are
   in its bottom row).
2. Create `mapping.conf` with

   ```
   probe on
   probestart 2/33
   ```

   The 7-segment display shows the id under test as `2   033` (panel 2, control 33).
3. **Knob 6** chooses the id (one click = ±1; hold SHIFT for ±16, so from `2/1` SHIFT steps visit 1, 17, 33, 49, …).
   **Knob 5** sends the turns to that id — watch which value moves in Resolve. **Knob 5's push button** resets the
   id under test.
4. As a check that probing works, `2/33` should move Saturation, `2/49` Hue, `2/65` Lum Mix and `2/81` Contrast
   (see the table). Pivot (`2/97`) is a poor check: with Contrast at zero it changes nothing you can see.
5. Write down the ids that do something. Set `probe off` (or delete the line) when done; all other knobs keep
   working normally during probing.

## Driving the probe from a script (`send` / `reset`)

For automated scans there are two one-shot commands: `send <pot> <delta>` sends a knob turn of `<delta>`
(-2000..2000) to the control `<pot>` as if the knob had been turned, and `reset <pot>` resets that control.
They run **each time the file changes** (not at start-up) and **only while `probe on` is in the file**, in the
order written, and the 7-segment display shows the id. A script should write the file *atomically* (write a
temporary file next to it, then rename it over `mapping.conf`) and uses a different delta to undo a step:

```
probe on
send 2/33 20       # run when the file is saved; the next write may say: send 2/33 -20
```

The file is checked five times a second. Remove the `send`/`reset` lines (or `probe on`) when finished.

### Scanning with a script

This is how the table above was made, and it works with any script that can talk to Resolve (the maintainer used
Resolve's own scripting API from Python). For each id: read the state of the node, write the file with
`send <id> 30`, wait about 0.4 s, read the state again, write `send <id> -30`, and check that the state is back.
The state to read is `Graph.GetToolsInNode()` (which group of parameters left its default) and `TimelineItem.GetCDL()`.
Group names are too coarse to tell Temp from Tint from Gain (all are "Primary Balance"), so for the ones that do
not name themselves export the current frame, shrink it with `sips -z 36 64 -s format bmp`, and compare the average
colour per brightness band before and after a larger turn (delta 300): Temp is a red/blue see-saw, Tint a green
versus red+blue see-saw, Highlights a change that grows with brightness. Several ids can be sent in one file to
search quickly, then split the groups that react.

## Example: Color Temp, Tint, Highlights, Shadows and Color Boost on SHIFT + the Offset-page knobs

This is the maintainer's setup (also in [mapping.example.conf](mapping.example.conf)):

```
shiftpot   2 1 2/8
shiftpot   2 2 2/9
shiftpot   2 3 2/24
shiftpot   2 4 2/25
shiftpot   2 5 2/26
shiftlabel 2 1 Color|Temp
shiftlabel 2 2 Color|Tint
shiftlabel 2 3 Hilights|
shiftlabel 2 4 Shadows|
shiftlabel 2 5 Color|Boost
```

With the Offset page (mode key 2) active, SHIFT + knob 1/2/3/4/5 then drives Temp/Tint/Highlights/Shadows/Color Boost
instead of Red/Green/Blue/Master Offset and Contrast, and the display shows the new labels while SHIFT is held. This only works while a
Custom Curves is open (see [the section above](#these-ids-need-custom-curves-to-be-open)), whichever palette is
selected on the left; otherwise the same knobs give anomalies (with Qualifier or Sizing open they move other
parameters).

## Example: the six Hue-curve colours on SHIFT + knobs 1–6 of pages 4, 5 and 6

Also in [mapping.example.conf](mapping.example.conf). Hold SHIFT and turn knobs 1–6 of mode key 3 (Hue vs Hue),
4 (Hue vs Sat) or 5 (Hue vs Lum) to move Red, Yellow, Green, Cyan, Blue and Magenta; without SHIFT the stock
functions stay. Mode keys are numbered from 0, so page *N* of the panel is mode `N-1` in the file. The three pages
are written the same way; this is Hue vs Hue (ids `2/0`…`2/5`), the others use `2/16`…`2/21` and `2/6`…`2/11`:

```
shiftpot   3 1 2/0
shiftpot   3 2 2/1
shiftpot   3 3 2/2
shiftpot   3 4 2/3
shiftpot   3 5 2/4
shiftpot   3 6 2/5
shiftlabel 3 1 Red|HvH
shiftlabel 3 2 Yellow|HvH
shiftlabel 3 3 Green|HvH
shiftlabel 3 4 Cyan|HvH
shiftlabel 3 5 Blue|HvH
shiftlabel 3 6 Magenta|HvH
```

It only works while a Hue-family curve is open in the Curves palette — see
[the Hue curves section](#hue-curves-the-six-colour-points-of-hue-vs-hue-hue-vs-sat-and-hue-vs-lum) for the limits.
The Page 3 example above is unaffected, so Temp, Tint, Highlights, Shadows and Color Boost stay where they were.

A complete annotated sample is in [mapping.example.conf](mapping.example.conf).
