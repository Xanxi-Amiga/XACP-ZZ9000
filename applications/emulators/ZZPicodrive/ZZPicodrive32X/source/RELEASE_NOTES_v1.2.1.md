# ZZPicoDrive32X v1.2.1 - release notes (1 November 2026)

Launcher update. The ARM Core1 program is the one of v1.2 (unchanged, `sub=6`
in the Shell log): same emulation, same speed.

## Controls

- CD32 pad and USB pads through Poseidon now give the 6-button functions:
  left / right shoulder = X / Y, yellow = Z (blue / red / green = A / B / C,
  play = Start). In v1.2 the yellow button was Mode and X / Y / Z were on the
  keyboard only, so the views of Virtua Racing Deluxe could not be changed
  from the pad. Mode stays on keypad 0.

## Start and diagnostics

- The start of the ARM program is checked: if it does not answer, the launcher
  reports it, parks the ARM core cleanly (another ZZ9000 program can run after
  it) and loads it once more. In v1.2 a start that did not answer left the ARM
  core running, and the next ZZ9000 program could crash.
- Started from the Workbench, the session log is written to
  `T:ZZPicoDrive32X.log`, and a failed start opens a requester with its reason.
- The "SH-2 PCs frozen" watchdog line of the Shell log only appears when the
  game runs the 32X (no false alarm with Mega Drive games).

The v1.2 release notes follow in [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md).
