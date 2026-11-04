# ZZPicoDriveMD Virtua Racing Edition 1.2

First release of **ZZPicoDriveVR**, dedicated to the original Mega Drive / Genesis cartridge edition of **Virtua Racing** (Europe, USA and Japan).

- Emulates the cartridge's SVP processor through PicoDrive's ARM recompiler on ZZ9000 Core1.
- Full-speed PAL / NTSC operation on the tested **A4000/060** (approximately 49.5 / 59.8 displayed frames per second).
- Correct full-width rendering of the game's 256-pixel video mode, with 50 Hz PAL / 59.98 Hz NTSC output. Optional scanlines using `ZZScanlines` or `ZZScanlinesGUI` from XACP firmware.
- CD32 / Poseidon USB six-button support; **C, X, Y and Z** select Virtua Racing camera views.
- Paula or AHI audio; five savestate slots; clean restart and Workbench error reporting.

**Requirements:** Amiga with a **68030 at 25 MHz or higher** and MNT ZZ9000 with **XACP XX19c or later compatible XACP firmware** and its matching Picasso96 driver (**XX19d-RIX or later compatible XACP firmware recommended**). **PAL mode requires a 50 Hz-capable monitor; otherwise use NTSC (60 Hz).** Other Mega Drive and 32X games are not supported by this dedicated edition.

No ROMs are included. PicoDrive's historical non-commercial licence applies; see the complete source and third-party notices.
