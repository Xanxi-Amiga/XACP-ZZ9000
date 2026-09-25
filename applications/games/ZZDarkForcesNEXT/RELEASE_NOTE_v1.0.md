# v1.0 release note

The final v1.0 launcher includes a SoundFont-discovery fix found during external testing by Fitzsteve.

`SC55` and `TIMGM6MB` no longer depend on a hard-coded list of exact SoundFont filenames. The closed-source 68k launcher scans the SoundFonts drawer for `.sf2` files and matches the requested SoundFont by a case-insensitive filename signature.

- `SC55` matches `.sf2` filenames containing `55`.
- `TIMGM6MB` matches `.sf2` filenames containing `TIM` or `GM6`.

This is a **launcher-side change only**. The GPL-covered Core1 blob and its corresponding public source are unchanged.

The same final launcher update also uses a safer AHI shutdown sequence by aborting both chained AHI requests before waiting for their completion.

The private 68k launcher source is intentionally not included in this GPL source archive.
