ZZOpenLara - WINDOWS MUSIC AND PSX SCREEN TOOLS
================================================

COMPLETE SOUNDTRACK (RECOMMENDED, 58 TRACKS)
  1. Double-click ZZOpenLara-Music-Complete.bat (or ZZOpenLara-Music.bat).
  2. Choose the PlayStation Tomb Raider I .CUE in the FIRST Windows file dialog.
  3. Choose the PC Tomb Raider I .CUE in the SECOND dialog.
     If you only have PC audio WAV files, choose the WAV folder option.
  4. The wizard runs a SINGLE conversion with PSX + PC and verifies
     all 58 tracks, including Wind and Pyramid (58 and 59).
  5. New file: tools/complete_music/ol_music.pak.
     Copy this file to ZZOpenLara/data/ on the Amiga after checking it.

The wizard is safe: it NEVER touches tools/ol_music.pak; if a previous
complete_music/ol_music.pak exists, it is renamed to a dated backup.
On failure, the new partial PAK is discarded.
Neither Python nor PyInstaller is needed by Windows users.
Windows PowerShell 5.1 is included with Windows 10 and 11.
Keep the BIN file(s) beside each CUE; a CUE file alone has no CD audio.

OPTIONAL PSX-ONLY SOUNDTRACK (56 TRACKS)
  Drag your PSX .CUE onto ZZOpenLara-Music.exe. This generates
  tools/ol_music.pak containing 56 tracks; it will REPLACE an earlier
  file of that name. DO NOT drag the PC CUE onto the same EXE afterwards
  expecting the two results to merge: it will overwrite the first PAK.

PSX LOADING SCREENS
  Drag the PSX .CUE onto ZZOpenLara-PSX-Screens.exe or its BAT wrapper.
  The five .RAW files appear in psx_screens/ next to the PSX disc image.
  Copy those five .RAW files directly into Amiga ZZOpenLara/data/.

PORTABLE ALTERNATIVE
  ol_music.py and ol_psx_screens.py are included for Linux and macOS
  or experienced users. Windows users do not need them.

No commercial Tomb Raider assets are included in this distribution.
