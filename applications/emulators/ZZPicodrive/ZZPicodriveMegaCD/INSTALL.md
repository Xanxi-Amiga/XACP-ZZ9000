# Installing ZZPicoDriveMegaCD 1.2

1. Use an Amiga with a **68030 at 25 MHz or higher** and an MNT ZZ9000. Install **XACP XX19c or later compatible XACP firmware** and its matching `zz9000.card` (**XX19d-RIX or later compatible XACP firmware recommended**). Completely power off the Amiga after a firmware or driver change. Official MNT and BlitterStudio (non-XACP) firmware branches are not supported.
2. Configure a ZZ9000 **320×240×32-bit** Picasso96 screen mode. **PAL mode requires a monitor supporting 50 Hz; otherwise select NTSC (60 Hz) in the launcher.**
3. Copy the `ZZPicoDriveMegaCD` distribution folder to the Amiga. The ARM program is embedded in the 68k executable. AHI is optional; **Paula audio is recommended on slower supported CPUs (such as a 68030 at 25 MHz)**.
4. Place the appropriate Mega-CD BIOS files in the included `BIOS` drawer, or select another BIOS folder in the launcher. The recognised filenames are listed in [README.md](README.md).
5. Copy legally obtained CD images to the hard disk. **CHD (`.chd`) is recommended**, or use `.cue` with its referenced tracks. For multiple discs, use `(Disc N)` filenames or an `.m3u` playlist.
6. Double-click `ZZPicoDriveMegaCD`, select image, region, audio backend and controller, then choose **Start**. **Shift+Esc** quits and saves backup RAM.

No BIOS or game data is included. **MP3 audio tracks and direct playback from physical Sega CD discs are not supported in this release.** See [README.md](README.md) for controls, formats, disc switching and limitations.
