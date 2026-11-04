# ZZPicoDriveMegaCD 1.2 — Sega Mega-CD / Sega CD Edition

First release of **ZZPicoDriveMegaCD**, running Mega-CD and Sega CD 32X games from disc images on the ZZ9000 ARM Core1.

- **CHD (recommended)** and **CUE/BIN** disc images, multi-disc `.m3u` playlists and F9 / Shift+F9 disc switching.
- Mega-CD BIOS selection by region, audio CD tracks, PCM sound, and Paula or AHI audio output.
- Internal backup RAM and five savestate slots; PAL 50 Hz and NTSC 59.98 Hz video output.
- Sega CD 32X support, tested with *Corpse Killer* and *Surgical Strike*.
- Tested on **A4000/060** with *Sonic CD*, *Dune* and *Dracula Unleashed*, including disc replacement during play.

**Requirements:** MNT ZZ9000, **XACP XX19c or later compatible XACP firmware** with matching `zz9000.card` (**XX19d-RIX or later compatible XACP firmware recommended**), Picasso96 320×240×32-bit mode, a **68030 at 25 MHz or higher**, and user-supplied BIOS and game images. **PAL requires a monitor supporting 50 Hz; otherwise select NTSC (60 Hz).** Prefer Paula audio over AHI on slower supported CPUs.

**Limitations:** MP3 audio tracks and direct playback from physical Sega CD discs are not supported in this release. ISO/WAV via CUE has not been independently validated on Amiga. Disc changes at a game's own change-disc prompt remain untested. Savestates are guaranteed only with the same region and audio output used when saving.

No BIOS or game images are included. PicoDrive's historical non-commercial licence applies; full sources and third-party notices are provided.
