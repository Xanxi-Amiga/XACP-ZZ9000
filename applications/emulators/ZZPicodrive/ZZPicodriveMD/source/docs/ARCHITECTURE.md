# ZZPicoDriveMD 1.2 - Architecture reference

## Execution model

The ZZ9000 Cortex-A9 Core1 runs PicoDrive (68k emulation through Cyclone,
Z80, VDP, YM2612 and PSG). The AmigaOS 68k application provides the file
system, ROM selection, controller input, Picasso96 display setup, audio
playback and save-state file handling. A fixed shared-memory interface
exchanges commands, frame ownership and PCM buffers.

The ARM program is included in the Amiga executable as a binary array.
Core1 source is in `Core1/`, AmigaOS launcher source in `amiga/` and the
upstream emulator tree in `third_party/picodrive/`. The shared address and
slot definitions are in `Core1/zzpico_shared.h`.

## Display and pacing

The emulation engine renders into private frame memory. Completed frames
are converted into a Picasso96 backbuffer, and a sequence number announces
when the buffer can be presented. The Amiga side uses three buffers and
waits for the firmware's deferred-PAN acknowledgement before recycling a
front buffer. A software vblank tracker estimates output phase and period;
the PAN acknowledgement counter also detects short vblank pulses that
could otherwise be missed while the 68k is interrupted.

PAL games use a 50 Hz video output and NTSC games use a custom approximately
59.98 Hz output, while preserving the Picasso96 320x240 screen and scaler.
`no50` and `no60` disable the respective custom timing adjustments.

Display cadence is measured separately from emulation cadence. Resource
contention and differing audio/video clocks can still cause occasional
missed display frames; measured results are in `RELEASE_NOTES_v1.2.md`.

## Audio

Core1 generates 22,050 Hz, 16-bit stereo PCM from the Mega Drive sound
hardware emulation. A shared PCM ring transfers it to the launcher, which
plays it through AHI or directly through Paula DMA. The launcher keeps
servicing audio during operations that require an ARM command acknowledgement,
including save-state operations. See the launcher implementation for the
backend-specific refill and priority rules.

## MMU and cache ownership

`Core1/zzpico_mmu.c` provides normal write-back/write-allocate mappings
for private ARM code and data and the 0x05F00000-0x05FFFFFF section
containing the firmware-provided Core1 stack. Shared control and audio
regions remain non-cacheable. Video buffers are cleaned for visibility
before they are presented; other cache maintenance accompanies the Core1
shutdown handshake. Do not make the control or PCM ring cacheable without
a redesigned cross-CPU coherency protocol.

## Start, stop and saves

The launcher sends INIT/LOAD/START and shutdown commands across the shared
interface, waits for acknowledgements, and stops Core1 before releasing
framebuffers. Save-state files use PicoDrive's format plus a small region
tag. A tagged state for the wrong PAL/NTSC region is rejected; save-state
compatibility across audio backends is not guaranteed.

## Versioning and reuse

Several field names and numeric build IDs retain their historical spelling
because they form part of the ABI or identify a specific Core1 binary.
They must not be renamed without updating the launcher and Core1 together.
The supplied reference ARM binary is included for comparison; a locally
recompiled blob can differ depending on the toolchain and libraries.

The PicoDrive and Cyclone sources retain their original copyright and
licensing terms. See `COPYING_PICODRIVE.txt` and `THIRD_PARTY_NOTICES.md`.
