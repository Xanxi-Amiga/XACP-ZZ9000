# Publication checklist — ZZSIDPlayGUI 1.0

- [x] Source tree is based on the Oct 22 validated 68k GUI with the A1 address
      fix, plus the A2 Core1 `$DE00` fix from the November test archive.
- [x] A1/A2 test results reported on the developer's Amiga for common 2SID
      addresses and 1SID, plus autoplay / Default Drawer.
- [x] Original Oct 22 ARM blob preserved separately for reference only.
- [x] Source package includes reSID, fake6502, shared ABI5 header and scripts.
- [x] Script uses fixed Windows toolchains and a 100% offline workflow.
- [x] No commercial SID tune files or incompatible playsid.library code included.
- [ ] Author confirms licensing of original Xanxi sources in light of GPL
      requirements from the bundled reSID / fake6502 components.
- [x] Freeze the exact developer-tested A2 executable (286936 bytes), SHA256
      f87ea5fd53cd7598c868b59bedd6aa13f065bd262ad14ee8cac6f8018dd02e08.
      The release asset is assembled directly from this tested binary, without
      rebuilding or altering it.
- [x] Developer-confirmed playback for 1SID and SID2 $D420/$D440/$D4A0/
      $D500/$D520/$D5A0/$DE00; autoplay and Default Drawer checked.
- [ ] Optional: verify a freshly built output from these sources separately,
      but DO NOT replace the validated release executable unless re-tested.
- [ ] Upload project directory to `applications/ZZSID/ZZSIDPlayGUI/` on `main`.
- [ ] Add root README application link; example in `integration/ROOT_README_ENTRY.md`.
- [ ] Upload `output/ZZSIDPlayGUI_1.0_AmigaOS3.zip` as a **GitHub release asset**.
- [ ] Create `ZZSIDPlayGUI-1.0` release tag using `integration/GITHUB_RELEASE_TEXT.md`.

DO NOT upload an A1 executable as version 1.0. Use exactly the validated
release asset whose embedded ZZSIDPlayGUI SHA256 matches the pinned hash.
The source build script may produce a different binary (compiler/version
variation); any rebuilt output must be tested before replacing the release.
