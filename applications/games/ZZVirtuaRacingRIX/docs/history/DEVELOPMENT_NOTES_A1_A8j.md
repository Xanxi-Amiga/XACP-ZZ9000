# ZZVirtuaRacingRIX A8j - test build

**A8j: the far view back at 6 tiles by default, 7 and 8 as options.** A8i on
the A4000 (8.5 min, 7 tiles): 5.6 % of the race frames kept their image, close
to 8 tiles' 6.1 %, while 6 tiles kept 1.4 % (A8g). The smoothest picture is
the default; flags `far7`, `far8` (and `far12`) reach further for those who
prefer it. Everything else as A8h/A8i (the PL330 doubles the background's
rows). The log shows `sub=32`.

## A8i

**A8i: the far view at 7 tiles by default.** A8h on the A4000, 8 tiles: the
background took 1.86 ms an image (A8g 2.16), but 6.1 % of the race frames still
kept their image (A8g at 6 tiles: 1.4 %). 7 tiles (haze 4778..6656) sits
between the two; flags `far6`, `far8` and `far12` as before. Everything else
as A8h. The log shows `sub=31`.

## A8h

**A8h: the far view at 8 tiles by default.** A8g on the A4000: 6 tiles kept
the image on 1.4 % of the race frames, 8 tiles (flag far8) on 7.1 %; 8 tiles
cost about 1.6 ms more per image (interpolation and triangles). A8h takes about
1 ms off the background instead: Core1 writes only the first of the two GPU
rows of each console line (286 KB instead of 573 KB an image), a quarter of
the picture at a time, and the PL330 copies each quarter's rows onto the rows
below them while Core1 writes the next quarter (librixgpu
`rix_dup_rows_start`: one program, nested loops, 64-byte bursts on 64-byte
aligned rows). Without the PL330, or if it fails, Core1 writes both rows as
in A8g. Same pictures as A8g on the PC bench.
- far view: 8 tiles by default (haze 5461..7680); flags `far6` (A8g's
  default), `far7`, `far8`, `far12`;
- flag `nodup`: Core1 writes both rows (A8g's background);
- the log's A7 lines say `background: Core1 writes one row of each line, the
  PL330 doubles it`. The log shows `sub=30`.

## A8g

**A8g: less Core1 time per image, and no dead end when RasterIX fails.**
A8f on the A4000 (10 min race): Core1 needed about 16.3 ms of the 16.7 of a
frame (emulation 5.3, interpolated list 3.8, background 2.5, triangles 3.7),
so 6.7 % of the race frames kept their image. A8g:
- **triangles**: a descriptor builder for flat triangles (librixgpu 1.6
  `rix_tri_desc_flat`): no texture, no depth, only the haze's alpha may vary,
  so the constant planes are written straight (same pixels);
- **background**: the sky's lines written straight into both GPU rows, two
  pixels a word (A8b built 640 pixels, then copied them twice);
- **interpolated list**: the SH-2's 64/32 division without the run-time
  library's 64-bit divide (the Cortex-A9 has no integer divider: the FPU
  gives the quotient, integers correct it; same results, tested on 2 x 10^8
  operands), the polygon expansion's neighbours without a modulo, the SH-2
  memory reader inlined;
- **RasterIX failing** (a GPU error or a fence that never comes): A6's patches
  are taken back, so the SH-2s draw the race again (20 images a second, as
  the plain emulator) instead of a race without its 3D; the log says
  `A6 given back to the SH-2s after a RasterIX error`;
- flags `lead4` / `lead5`: Paula 4 or 5 frames ahead (92 / 115 ms; default 3,
  69 ms). The A8f log had 4 audio underruns, each at a moment the system held
  the 68k for 73 to 85 ms.
Pictures identical to A8f's on the PC bench (and check mode identical). The
log shows `sub=29`.

## A8f

**A8f = A8e without the frozen image.** A8e on the A4000: after a 51 ms
stall of the 68k, the image froze until the end of the race (the game and
the sound went on). The governor (A8a) holds the image when the PCM ring is
low, so that Core1 catches up; but when frames wait for the 68k to take the
previous one (deferred publication), the ring is allowed to sink to 700
pairs before PicoFrame goes on, so it stayed low without Core1 being late,
and the governor held the image for good. Now it holds only when Core1
itself is late (the pacing loop before PicoFrame found the ring already
below its target) and never twice in a row: at worst 30 new images a
second for a moment, never a frozen picture. The log shows `sub=28`.

## A8e

**A8e = A8d with the real cause of "A7 off" fixed.** The launcher writes the
screen's format (16-bit big-endian) after START, once its screen is open;
the Core1 decided whether to draw an image every frame at START, so it read
the slot as the previous Core1 program had left it. After another run of
this program that was the right value (A7a to A8b worked); after Half-Life
it was not, and A8c/A8d went the A6c way (20 images a second). The Core1 now
reads the format at every frame. The A8d log line `needs the 640x480 16-bit
screen and the PL330` was wrong about the PL330: it was the format. The
Core1 copy without the PL330 (A8d) stays, for safety. The log shows
`sub=27`.

## A8d

**A8d = A8c that no longer depends on the PL330.** On the A4000 the A8c run
found the PL330 DMA controller not clocked (or held in reset) at start: A7
was then switched off, and the race went the A6c way (20 images a second,
the emulator's sky, no blurred wheels). A8d keeps the 60 images a second in
that case: Core1 copies the 3D lines itself (same pictures, bit for bit on
the PC bench). The log's `A7 images` line then says `copied by Core1 (PL330
not clocked or in reset at start)`, and the `per frame` line gives the copy
time. The log shows `sub=26`.

## A8c

**A8c: fast wheels, and the list built while RasterIX draws.**
- **wheels**: from about 100 km/h on, the game turns them by the same step
  whatever the speed. A8b showed that step at every frame: nearly half of
  one of the wheel's eight sides, so it read as a slow turn, worst at
  300 km/h. Now, from that speed on, each image turns the wheel forwards by
  an amount that grows with the car's speed (up to nearly the most a picture
  can show going forwards), and draws it twice, the copy turned a sixteenth
  of a turn further at 50 %: the eight sides blur into a round tyre that
  spins faster as the car goes faster. Player and opponents. Below that
  speed, interpolated as before. Flag `nowheels` for A8a's;
- **timing**: A8b kept the image on 11 % of the race frames (1601 of about
  14300) while Core1 waited 1.9 ms a frame for RasterIX to finish the image
  before. The interpolated list (3.6 ms) is now built right after PicoFrame,
  while RasterIX is still drawing; the wait comes after it.
Everything else as A8b (same pictures with `nowheels`). The log shows
`sub=25`; the `A8 sky` line ends with `wheels blurred and spun with the
speed above 100 km/h`. Worth checking in the log: `images not drawn because
Core1 was behind` (A8b: 1601) and `fence wait avg` (A8b: 1914 us).

## A8b

**A8b: the sky, the wheels, and back towards 60 images a second.**
A8a (far view, haze, detailed opponents) ran at 59.8 fps but the governor
kept the image on 10 % of the race frames (about 54 images a second): the
lists took 4.0 ms an image and the triangles 3.8 ms. A8b:
- **sky as VirtuaRacingDeluxeEnhanced shows it**: plane B (the sky and its
  clouds) is drawn by the Core1 from the VDP's memory into a cache, moved
  sideways at every image with the interpolated camera (A8a: it jumped 20
  times a second while the road turned smoothly), in half pixels, and its
  A-B-A dither blended into gradients (VRDE's skyblend shader, in integer
  arithmetic, 8 rows a frame after each change of the sky). It is used only
  while plane B as drawn is what the console shows; otherwise the emulator's
  sky as before. Flags `noblend` (moved, dither kept), `nosky` (as A8a);
- **wheels**: at racing speed the game turns them by a fixed step of about
  0.44 turn a tick, a strobe at its 20 pictures a second; interpolated, they
  turned slowly. Each frame now takes the tick's whole step, as each of the
  game's pictures did (below a quarter turn a tick, interpolated as before).
  Flag `nowheels` for A8a's;
- **lists**: the HLE cleared an 800 KB structure for each list (40 % of its
  time on the PC bench); now only its counters. Same lists, bit for bit.
The log shows `sub=24`; `=== VR RasterIX ===` has an `A8 sky` line.

## A8a

**A8a: a better picture.** A7b reached 59.8 fps with the race 3D at 60
interpolated images a second and about 4 ms to spare. A8a turns on what
VirtuaRacingDeluxeEnhanced's HLE adds to the picture:
- **far view**: the track and its props drawn up to 6 tiles (6 x 1024 world
  units) from the car instead of about 4000 units: no more scenery popping up
  at the horizon; flags `far8` / `far12` reach further (12 is VRDE's default),
  `nofar` keeps the original's range;
- **haze**: far away the 3D fades into the sky (from 4096 units to the far
  view's end, minus half a tile), as VRDE's far_view 1; `farnohaze` without;
- **detailed opponents**: every car with the detailed model and wheels the
  game gives only the nearest; `nodetail` for the game's simpler ones.
The far view doubles the polygons at 8 tiles (+50 % at 6), so A8a also adds:
- an **image governor**: when Core1 runs late (the PCM ring falls below 1300
  stereo pairs) the frame keeps the image on screen instead of drawing a new
  one; the images a second drop for a moment, never the sound;
- the GPU buffers' lines are now write-back **without write-allocate** for
  Core1 (the 491 KB prefill no longer reads every line first).

The log's `A8` lines give the settings, the polygons per image and how many
images the governor skipped. The log shows `sub=23`.

## A7b

**A7b = A7a made lighter.** A7a worked on the A4000 (9185 interpolated images
in 171 s, PL330 copy without an error, 0.1 ms wait) but at 59.2 fps with 1187
underruns: about 15.4 ms a frame on average, more in the frames that bring a
game tick. A7b:
- no longer builds a full polygon list for each scene the 68000 sends (2.5 ms
  in the tick's frame, inside PicoFrame): the pictures come from the
  interpolation only, the scene's list keeps just its 3D lines;
- keeps the image on screen when the interpolation gives the same list (so
  `i40` and `i30` now save the GPU and Core1 work of the frames between two
  images; A7a redrew them);
- fills the GPU buffer one line then the other (A7a interleaved the two
  lines: 2.1 ms), writes back the HUD's cache lines once each, and cleans
  only the P96 lines Core1 wrote (the PL330 writes the others).
On the PC bench the images are identical to A7a's. The log shows `sub=22`
and, in the A7 block, how many frames kept the image.

Test as A7a (`usa paula`, demo and a race), then with `i40` if it still
crackles.

## A7a

**A7a: the race 3D at 60 images a second.** A6c ran at 59.8 fps with no
underrun, but the 3D moved at the game's 20 images a second. A7a draws a GPU
image at the end of every frame of a race, with the camera and the cars
interpolated between the game's last two ticks (VRDE's `hle_interpolated`):
the game itself (physics, AI, timing) is untouched, only the pictures between
its ticks are added.

To pay for it, Core1 no longer reads the GPU's picture (A6c: 4.2 ms a frame):
- before the GPU draws, Core1 fills the GPU buffer with the emulator's
  picture doubled (the sky shows where the GPU draws nothing), and the GPU
  draws over it without a clear (3 ms less GPU time per image);
- once the GPU is done (next frame), Core1 writes the HUD's pixels over it;
- the PL330 DMA copies the 3D lines into the P96 buffer and swaps the bytes
  of each pixel for the big-endian screen (about 0.5 ms, while Core1 writes
  the other lines). If the PL330 ever fails, Core1 does the copy itself and
  the log says so.

Flags: `noa7` back to A6c (one GPU image per game tick), `i40` 40 images a
second, `i30` 30 images a second. The log (`=== VR RasterIX ===`) has an
`A7 images every frame` block: images, per image interpolation / prefill /
triangles+submit, per frame the wait for the PL330. The log shows `sub=21`.

What to check: smooth 3D motion in the race and the demo, the HUD and the
sky as in A6c, 60 fps, no underruns. If it stutters, try `i40` then `i30`
and send the logs.

## A6c

**A6c = A6b with the merge rule the A6 picture needs.** The A6b log from the
A4000 proved A6 in effect: both race commands patched, 0 slave commands,
master 0.8 ms and slave 0.05 ms a frame, PicoFrame 5.8 ms, 59.7 fps, 0 Paula
underruns. But the screen showed only the HUD, the sky and a green lawn: the
merge kept every emulator pixel that was not the frame buffer's own colour
(its rule for "the Mega Drive layer is in front"), and with the frame buffer
cleared by A6 that is the whole Mega Drive backdrop. On the PC bench every
colour the game's polygons use carries the 32X priority bit, so in the
original no Mega Drive pixel is ever in front of a polygon. A6c's rule when A6
is on: the GPU's pixel everywhere it drew one; the emulator's pixel only for
the HUD's bitmaps and where the GPU drew nothing (the sky, where the Mega
Drive layer showed through index 0 before). The frame buffer index map is no
longer built (0.3 ms a picture). The "pixels equal" statistic has nothing to
compare with and says so. The log shows `sub=20`.

What to check: the race as in A5b (road, cars, scenery, needle, course map,
sky with the Mega Drive clouds), the countdown and the signs over the 3D, 60
fps, no underruns; title, menus, car and course select, results untouched.
Send the whole log as text.

## A6b

**A6b = A6a plus diagnostics of the A6 patches.** The first A6a log from the
A4000 showed PicoFrame unchanged (11.2 ms) and the GPU-vs-SH-2 pixel match at
92.5 %, two signs that the SH-2s were still drawing. A6b adds to the
`=== VR HLE ===` block the words read every frame at the two race patch
addresses (`words at 06000D06/06000D1C`: `a003/a012` once patched,
`9124/d013` untouched) and a count of the master's commands to the slave
while the 68000 asks for a race picture (`slave commands during race
pictures`: 0 when the patches are in effect). The patch mechanism itself was
re-checked on the PC bench with the SH-2 recompiler (x86-64 backend, same
block invalidation as on the Core1): identical game, 0 slave commands after
the patch, the demo twice as fast. The log shows `sub=19`.

What to send: the whole log as text, from the first line (version, `sub=`)
through `=== VR HLE ===` (`SH-2 time per frame`, `A5 busy-wait patches`,
`A6 race without the SH-2s`, `words at`, `slave commands`) to the summary.

## A6a

**A6a: the race without the SH-2s.** During a race the two SH-2s only draw:
the 68000 runs the game (physics, AI, input, rules, Mega Drive sound) and
sends it a scene each tick of 3 frames; the HLE rebuilds the same polygon
list from that scene and RasterIX draws it. A6a patches the master's two race
commands in memory (`Core1/zzvr_a6.c`, same method as A5b) so that they keep
the scene transfer, the HUD's bitmaps and the "picture finished" signal, and
skip the drawing: the slave is never commanded and sleeps, the master returns
to its command loop and sleeps too. The 3D lines of the frame buffer are
cleared by the Core1 program instead of the SH-2s' fills, so the Mega Drive
layer shows through where the GPU draws nothing, as before.

On the PC bench (demo of 3000 frames and a race of 7200 frames): the game
runs the same (identical scenes, identical frame buffer outside the 3D,
identical frame buffer swaps, every tick 3 frames as before), the slave runs
0 instructions and the master 95 % fewer in a race (84 % in the demo, whose
ranking panel it still copies into the frame buffer).

Flag `noa6` turns it off (the SH-2s draw the race as before; `check` then
compares again). The log says whether both commands were patched and counts
the game's ticks (`=== VR HLE ===`). What to read: `SH-2 time per frame`
(both should be well under 1 ms), `laid over the 32X picture`, `FPS=`,
underruns, and the pictures: the race must look as in A5b (3D by the GPU,
HUD, course map and rev counter needle) and the screens around it (title,
menus, car and course select, results) must be untouched. The log shows
`sub=18`.

## A5b

**A5b: the SH-2s stop wasting time.** Measured on the A4000 (A3b4m): the
slave SH-2 costs 2.9 ms and the master 2.7 ms of each PicoFrame. Counted on
the PC test bench, 47 % of what the slave executes and 12 % of the master's is
two busy-wait loops of the game: the slave's wait for a command (64 delay
rounds between two reads of COMM4, too slow for PicoDrive to see it polling)
and a 125-round delay after each 32X VDP fill. A5b patches those two loops in
the game's SH-2 code in memory (`Core1/zzvr_a5.c`): the slave now reads COMM4
back to back and PicoDrive puts it to sleep until a command arrives; the fill
delay is 1. On the PC bench the game runs the same (same scenes, the same 806
race pictures identical to the HLE's) with 62 % fewer slave instructions.
Flag `noa5` turns it off; the log says whether both patches were found
(`=== VR HLE ===`). The log shows `sub=17`.

## A3b4m

A3b4m = A3b4 plus the first measurement of step A5: the time the master and
the slave SH-2 take inside each PicoFrame (block `=== VR HLE ===`, line
`SH-2 time per frame`). The slave draws the 3D that RasterIX now draws again;
its share says how much A5 can give back. The log shows `sub=16`.

## A3b4

**A3b4 = A3b3 with a faster 640 x 480 merge.** A3b3 ran 151 s without a crash
(the GPU's colour buffers back in the GPU window), but at 55 fps with crackling
sound: laying the GPU's 3D over the doubled picture took 5.2 ms a frame (1.4 ms
without the GPU). A3b4 copies the GPU's lines as they are (byte-swapped with one
instruction per two pixels), then writes over them only the pixels that must
show the emulator's picture (HUD, Mega Drive layer in front). The match
statistics are taken on one picture in four. The log shows `sub=15`.

## A3b3

**A3b3 = A3b2 with the GPU's colour buffers back in the GPU window**
(0x06600000 / 0x06696500, 640 x 481, where ZZOpenLaraRIX and ZZQuake2RIX keep
theirs). A3b2 had them in the Core1 arena and reset-looped the A4000 in a race
after 82 s; with `norix` (same 640 x 480 x 16 screen, no GPU) it ran 261 s
without a fault or a crackle. After the GPU's init, the window is switched to
4 KiB pages so that the Core1 still reads the picture lines through its cache
(the fence lines stay uncached). The log shows `sub=14`.

## A3b2

**A3b2 = A3b1 in 16 bits.** A3b1 (640 x 480 x 32) lost a sixth of the frames
on the A4000: writing the 32-bit picture took 7.2 ms a frame (1.2 MB). The
640 x 480 screen is now 16-bit RGB565, the format of both the GPU and the
emulator's output: the Core1 program only copies (614 KB a frame, no
conversion). The launcher reads the screen's pixel format (`pixel format:` in
the log); a big-endian RGB565 screen is byte-swapped by the Core1 program on
the way. Flag `r320` keeps the 320 x 240 x 32 screen of A3a. The log shows
`sub=13`.

## A3b1

**A3b1: 640 x 480.** The launcher opens a 640 x 480 screen (flag `r320`: the
320 x 240 screen of A3a). RasterIX draws the race 3D at 640 x 448, twice the
console's resolution, from the same polygon list (VRDE's settings for that
scale); the Core1 program writes the 640 x 480 picture itself: the emulator's
320 x 240 output doubled, and in the 3D lines the GPU's pixels where the 3D is
(not over the HUD, not where the Mega Drive layer is in front). Edges, the road
lines and the cars are drawn at the higher resolution; the HUD and the Mega
Drive layer (sky, texts) stay doubled.

How: two GPU colour buffers in the Core1 arena (0x3EA00000, 0x3EB00000, frame
layout 640 x 449, fence on line 448), mapped inner write-back / outer
non-cacheable except the 4 KiB page of the fence line, so that the Core1 reads
the GPU's picture through its L1 cache (invalidated when a new picture comes)
instead of uncached; no read back any more. The log shows `sub=12`.

Expected cost: about 4 times more pixels to write per frame (1.2 MB). The log
line `conversion=` and `laid over the 32X picture` say what it costs; frames
lost or audio underruns mean it is too much for now (next steps: a 16-bit
screen, A5).

Test: `usa paula` (640), then `usa paula r320` (as A3a4) for comparison, then
`usa paula rixonly` to see the GPU's 3D alone. Photos of the 640 screen help.

# ZZVirtuaRacingRIX A3a4 - test build

A3a4 = A3a3 (which ran on the A4000: 1114 pictures drawn by RasterIX, 0 GPU
error, 93.45 % of the pixels equal to the SH-2s') with the GPU picture read back
in 32-byte bursts instead of word by word (A3a3: 4.7 ms per picture, which cost
audio underruns and a few frames), and the cost of the HUD/index maps shown
apart. The log shows `sub=11`.


A3a3 = A3a2 with the GPU's chunk ring moved out of the GPU window into the Core1
arena (0x3E800000, 8 x 64 KiB, non-cacheable), as ZZQuake2RIX and ZZOpenLaraRIX
have it. In A3a/A3a2 the ring (0x067E3000, 7 x 16 KiB) reached the firmware's
staging area at 0x067F0000; the first race picture, the first frame long enough
to use those slots, hung the GPU (8 lost frames, then RasterIX stopped, and
ZZOpenLara could not start before a power cycle). The log shows `sub=10`.


A3a2 = A3a plus GPU init diagnostics: on the A4000, A3a's RasterIX init stopped
at ladder stage L8 (rc 0xE1B8, the first frame with a fence). A3a2 prints the
whole init ladder and librixgpu's diagnostic block in the log
(`=== VR RasterIX ===`), and runs the init a second time when the GPU still
takes commands (as a second launch would). Flag `rixsd` prints the block even
when the init works. The log shows `sub=9`.


Virtua Racing Deluxe (32X) on the ZZ9000 Core1, step A3a: **the race 3D drawn
by RasterIX**. Each race picture the 32X puts on screen is drawn again by the
GPU from the polygon list of the HLE (step A2), the way VirtuaRacingDeluxeEnhanced
draws it with OpenGL, and laid over the 32X picture, still 320x240.

The SH-2s still draw their own picture (they stop in step A5); the GPU's
picture replaces it pixel by pixel. So at this step the picture looks almost
the same as before. What changes on screen:

- **shadows and smoke are half transparent** instead of the checkerboard
  dither of the 32X (VRDE's default);
- a few pixels along polygon edges (exact vertices instead of the SH-2s'
  rounded ones).

To see the GPU's picture on its own, flag `rixonly`: the 3D pixels the GPU
leaves empty are shown black (cracks between polygons, if any, show up).

Base: A2a (ZZPicoDrive32X v1.2.1 + the HLE of VRDE), plus:

- `Core1/zzvr_rix.c`: the GPU path. Exact vertices, polygons pushed out by
  0.375 pixel (VRDE's `expand_polygon`, `Core1/vrde/expand.c`), fans of
  triangles in the list's order, clipped to the 3D lines, flat colours from the
  32X palette, dithered polygons blended at 50 %. The GPU draws while the next
  frame is emulated; its picture is read back once and laid over the emulator's
  output on every frame it stays on screen (not over the HUD pixels, not where
  the Mega Drive layer is in front).
- `Core1/librixgpu/`: librixgpu 1.5 of ZZOpenLaraRIX (0BSD), unchanged. Init
  without the texture stages, the chunk ring in the GPU window, two colour
  buffers. The GPU window 0x06600000-0x06800000 is mapped non-cacheable.
- Colour calibration at start: 64 colours drawn three ways, the exact one is
  kept (the log says which).
- The A2 check is now off by default (flag `check` turns it on); it logs its
  first pictures that are not identical.

Core1 program: `reference/zzvr_a3a_reference.bin` (the log shows `sub=11`).

## ROM

Virtua Racing Deluxe (USA), CRC32 `7896B62E` (the launcher checks it). No ROM
is included.

## Test on the A4000

From the Shell, in the drawer of the program:

```text
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula            (1) RasterIX
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula rixonly    (2) RasterIX only
ZZVirtuaRacingRIX "Virtua Racing Deluxe (USA).32x" usa paula norix      (3) as A2a nocheck
```

For each: let the demo run once, then play a race (2-3 laps, change the view
with X/Y/Z, drive behind other cars to see their shadows), Shift+Esc. Send the
end of the log (from `=== BLOB DIAG ===`). A photo of the screen during (1) and
(2) helps.

Block `=== VR RasterIX ===` of the log:

- `init: rc=0x0000 caps=...`: the GPU answered. `STOPPED` means the init or a
  frame failed: the SH-2 picture is shown alone, nothing else breaks.
- `colour calibration: mapping N exact`: one of the three colour mappings gives
  every colour exactly. `NOT EXACT` would show as slightly wrong colours.
- `pictures drawn`, `triangles avg/max`.
- `Core1 per picture`: what the GPU path costs the ARM per race picture (20 a
  second): the maps and the triangles, the wait for the GPU (should be about
  0: it finished during the next frame), the read back.
- `laid over the 32X picture`: the cost on every frame.
- `GPU pixels equal to the SH-2s'`: on the PC test bench about **95 %** (VRDE
  measured 94.9 % with the same settings); the rest are edge pixels and the
  blended shadows. Below 90 % something is wrong.
- `GPU errors` must be 0.

Compare `PicoFrame window avg/max` and the frames lost in `=== SUMMARY ===`
of runs 1 and 3: the GPU path should cost a few milliseconds per picture, not
a lost frame.

Flags of this build, besides those of ZZPicoDrive32X: `norix`, `rixonly`,
`check`, `nohle` (`nocheck` is the default now).

## Build

```text
M68K_GCC="C:/amiga-gcc/bin/m68k-amigaos-gcc" ./build_release.sh
```

Step 1 compares the Core1 program with the reference; a different MD5 means a
different toolchain build (`BLOB=reference/zzvr_a3a_reference.bin
./build_release.sh embed` then `./build_release.sh launcher` embeds the
reference itself). The result is `release/ZZVirtuaRacingRIX/`.

Firmware: XX19d-RIX. Nothing in the firmware, bitstream or XACP protocol is
changed.
