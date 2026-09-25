# SoundFont license notes

The SoundFonts used by ZZDarkForcesNEXT are separate data files with their own licenses.

## Roland SC-55 SoundFont

The SC-55 SoundFont copy used during development (stored as `Roland SC-55.sf2`) matches the SC-55 SoundFont distributed by ScummVM. The final launcher does not require this exact filename.

ScummVM attributes it as:

```text
Roland_SC-55.sf2
Copyright (c) 2015 deemster
Licensed under GNU GPL v3 or later.
```

Reference file used during development:

```text
size    3283278 bytes
MD5     dbb97c34404b753724a66fe0108a32e1
SHA1    9dba9212f2e34446ab034595665809f09f303d63
SHA256  fca3e514b635a21789d4224e84865d2954a2a914d46b64aa8219ddb565c44869
```

If this SoundFont is distributed with the player release, include its copyright notice and GPL-3-or-later license text.

## TimGM6mb SoundFont

The TimGM6mb SoundFont used during development (commonly named `TimGM6mb.sf2`) is distributed under GNU GPL v2. The final launcher does not require this exact filename.

Known attribution:

```text
Copyright 2004 Tim Brechbill
Copyright 2010 David Bolton
License: GNU GPL v2
```

If it is distributed with the player release, include the corresponding copyright notice and GPL-2 license text.

## Packaging note

The GPL source archive does **not** include either SoundFont. They belong in the player package only if you choose to redistribute them with their respective license notices.

The public launcher supports both `SC55` and `TIMGM6MB`. It scans the `SoundFonts` drawer for `.sf2` files instead of requiring one exact filename: `SC55` matches filenames containing `55`, while `TIMGM6MB` matches filenames containing `TIM` or `GM6`, case-insensitively.
