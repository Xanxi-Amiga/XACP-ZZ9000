# ZZScreensavers 1.1 — classic AmigaOS 3.x / ZZ9000 / XACP

ZZScreensavers includes two classic Core1 screensavers, Earth and Julia, plus the new RasterIX-accelerated **Solar Winds** port. The 68k Amiga handles display, keyboard/mouse input and OS integration; the ZZ9000 Cortex-A9 Core1 performs the simulation and rendering preparation, while Solar Winds uses the integrated FPGA RasterIX renderer.

| Executable | Feature | Firmware |
|---|---|---|
| `ZZScreensaverE` | Rotating textured Earth, 640x480 | XX19b or later |
| `ZZScreensaverJ` | Animated Julia fractal, 320x240 | XX19b or later |
| `ZZScreensaverS` | Solar Winds, RasterIX GPU, 640x480 RGB565 | **XX19d-RIX** |
| `ZZScreensaverHotkeys` | Optional Commodities hotkeys | Depends on selected saver |

### Hotkeys and presets

Install the four executables into `C:`. Add to `S:User-Startup` or retain the existing 1.0 command:

```text
Run >NIL: C:ZZScreensaverHotkeys
```

Quit the old commodity in Exchange before restarting it, or reboot after copying the new version. The helper provides Ctrl-Alt-E (Earth), Ctrl-Alt-J (Julia), Ctrl-Alt-S (Solar Winds, random among presets **1, 2, 3, 4, 6**, excluding the last accepted preset request). This is a *manual hotkey helper*, not an idle-time screen blanker.

The Solar Winds program supports six manual presets:

| Preset | Name |
|---|---|
| 1 | Regular (default) |
| 2 | Cosmic Strings |
| 3 | Cold Pricklies |
| 4 | Space Fur |
| 5 | Jiggly (**not in Hotkeys Random**) |
| 6 | Undertow |

```text
ZZScreensaverS
ZZScreensaverS PRESET 3
ZZScreensaverS PRESET 6
ZZScreensaverS DEBUG PRESET 3 > RAM:ZZSW_3.txt
```

Solar Winds is silent by default. The A1 launcher implements mouse-pointer hiding/restoration and exit on keyboard/mouse activity. The hardware measurements inherited from A0 cover presets 1–5; results for the final 1.1 binary and preset 6 should not be inferred from host simulation. No fixed 60 FPS claim is made. Only one dynamically loaded Core1 application should be active at a time.

### Requirements

Classic AmigaOS 3.x with ZZ9000, Picasso96 and the compatible XACP `zz9000.card` driver. XACP **XX19d-RIX** firmware is required to enable FPGA-accelerated Solar Winds. Firmware and driver installation instructions are provided separately with the firmware release.

### Downloads

- `ZZScreensavers-1.1.lha` — full Amiga package, including the original Earth/Julia binaries and icons, uploaded 1.1 Solar Winds and Hotkeys binaries, and license notices.
- `ZZScreensavers-1.1-GPL-source.zip` — corresponding source for Solar Winds (68k launcher + ARM Core1 + RasterIX transport and build scripts).
- `ZZScreensavers-1.1-SHA256SUMS.txt` — release-archive checksums.

### Licensing and provenance

`ZZScreensaverE`, `ZZScreensaverJ` and `ZZScreensaverHotkeys` remain separate Xanxi-owned proprietary freeware executables. Their permissions are described in `LICENSE_ZZSCREENSAVER.txt`. The Earth texture uses NASA Visible Earth / Blue Marble imagery, credited in `NASA_CREDITS.txt`.

**Solar Winds** is derived from the original screensaver by Terence M. Welsh (1999–2010) and distributed as **GPL-2.0-or-later**, including its corresponding 68k and Core1 source. The XACP adaptation is by Xanxi (2026). 0BSD XACP support components preserve their original notices. See `THIRD_PARTY_LICENSES.txt`, `source/SolarWinds/COPYING`, and `source/SolarWinds/PROVENANCE.md`. RasterIX GPU by ToNi3141; MNT ZZ9000 by MNT Research GmbH. This is an independent project.

Previous Release: [ZZScreensavers 1.0](https://github.com/Xanxi-Amiga/XACP-ZZ9000/releases/tag/ZZScreensavers-1.0).
