# Installing ZZPicoDriveVR 1.2

1. Install **XACP XX19c or later compatible XACP firmware** and its matching `zz9000.card` (**XX19d-RIX or later compatible XACP firmware recommended**). After changing firmware or the driver, completely power off the Amiga before restarting. Official MNT and BlitterStudio (non-XACP) firmware branches are not supported.
2. In Picasso96, provide a ZZ9000 **320×240×32-bit** screen mode. **PAL requires a monitor supporting 50 Hz**. If your monitor does not support 50 Hz, select **NTSC (60 Hz)** in the launcher.
3. Use an Amiga with a **68030 at 25 MHz or higher**. Copy the `ZZPicoDriveVR` distribution folder anywhere on your Amiga. The ARM Core1 program is embedded in the executable; no separate ARM file is needed.
4. Provide your own legally obtained **Virtua Racing** ROM (`.bin`, `.gen`, `.md`), from any region. No ROM is included.
5. Double-click `ZZPicoDriveVR` and choose ROM, region, audio backend and controller. Press **Start**. Use **Shift+Esc** to quit.

Paula sound needs no extra software; install AHI only if you wish to use it. See [README.md](README.md) for controls and savestates.
