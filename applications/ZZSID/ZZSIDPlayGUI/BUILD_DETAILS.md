# Build details / reproducibility

The release build is **fully offline** and targets the Windows MSYS environment.

## Preferred command

```sh
sh build_all.sh
```

It calls `python build.py`, then `python package_release.py` after a successful
cross-build. No `PATH` export, `git`, `curl`, `wget`, `cp`, `rm`, `awk` or
other external packaging tools are required in the scripts.

`build.py` compiles `sources/ARM/zzresid_core1.cpp`, fake6502, reSID and
`startup.S` for Cortex-A9, links with `sources/ARM/zzresid_core1.ld` including
`.ARM.extab`, `.ARM.exidx`, `__exidx_start` and `__exidx_end`, generates the
bare-metal payload, then embeds it into the Amiga 68k launcher.

The launcher's 68k output uses `-O2 -m68020 -mcrt=nix20` and `-lamiga`.
The ARM build uses `-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard`.

## Tests without cross compilers

```sh
python build.py --check
python tests/test_routing.py
```

These are **static** checks. They are not substitutes for actual Amiga playback.

## Bundled original ARM reference

`sources/zzresid_core1_WORKING.bin` is the exact 261,448-byte Core1 blob
extracted from the **22 October 2026** validated GUI (SHA-256
`933eae03c0e93aadc1ddc467ecf62d15f7f69b177d49c1c7dcf5eacf3ec4324f`).
It is used **only for provenance / historical A1 builds**. A2, and the public
1.0 version, compile a new ARM payload from `sources/ARM/`.

## Release integrity

The final 1.0 **Amiga binary is generated on the author's Windows/MSYS
installation** using the complete local toolchains. It has not been
cross-compiled in the environment that assembled this GitHub staging package.
Keep and verify the generated `output/ZZSIDPlayGUI` and `SHA256SUMS.txt` before
publishing the release asset.
