# ZZMiniGL Beta 1 - GitHub release notes

**AmigaOS 3.x / 68060 / ZZ9000 / XACP v1.8 / XX19d-RIX / RasterIX**

First public beta of `minigl.library` for a physical MNT ZZ9000. It executes a functional subset of MiniGL 29.1 (dispatch ABI 5) by retaining the original 68k application code on the 68060 and offloading rendering to ARM Core1 and RasterIX.

**Demonstrated compatibility:** unmodified GLQuakeR200 1.5.1, classic MiniGL GLQuake with AHI, and RadeonGL/PiStorm3D demonstrations. The unmodified PiStorm3D `gears` program and `data/chrome.ppm` are included; the other third-party demos must be obtained separately. A8.29 was measured at **22.2 FPS** in GLQuakeR200 `timedemo demo1`, 640x480, AHI, on A4000/CyberStorm MK II 68060 50 MHz/ZZ9000. Additional measured resolutions and test-version details are in README.md.

**Limits:** 115 implemented handlers of 205 dispatch entries, one context, 16-bit modes (320x240, 640x480, 800x600), one physical texture unit, no mipmaps. This is not a complete MiniGL/OpenGL implementation.

**Release assets:**
- `ZZMiniGL_Beta1_Amiga.zip`: the **matching** `minigl.library` and `ZZMiniGLLoad` A8.29 binaries, AmigaDOS launch scripts, the PiStorm3D gears demo with its chrome texture, documentation and original license notices.
- `ZZMiniGL_Beta1_Source.zip`: corresponding library/loader sources including embedded Core1 program, generator scripts, third-party licenses and offline MSYS build.

**Important:** Keep the two distributed executables from the SAME archive, in the target program drawer. Do not install the beta library in `LIBS:`. Other third-party demos, games and commercial Quake data are not included. The PiStorm3D gears demo is included unmodified with its required texture and upstream notices.

**Credits:** MiniGL interface - Steffen Haeuser / Hyperion; RadeonGL - Marcus Gerards; RasterIX - ToNi3141; ZZ9000 - MNT Research; ZZMiniGL/XACP - Xanxi.
