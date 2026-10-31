# Master System games with YM2413 FM sound

List from the ConsoleMods wiki ("Master System: FM Audio Information"). Set
**FM sound** to On (default). The game decides to use the FM unit: the
`FM:` line printed at exit (Shell start) shows `fm_ctl=0x01` and thousands of
register writes when it does.

## Officially supported

Released in Japan with advertised FM Sound Unit support.

- After Burner
- Alex Kidd BMX Trial
- Alex Kidd: The Lost Stars
- Alien Syndrome
- Aztec Adventure / Nazca '88
- Blade Eagle 3-D
- Bomber Raid
- Captain Silver
- Cyborg Hunter / Chouon Senshi Borgman
- Double Dragon
- Fantasy Zone II
- Fantasy Zone: The Maze / Opa Opa
- Galactic Protector
- Global Defense / SDI
- Great Golf / Masters Golf
- Golvellius: Valley of Doom
- Hoshi wo Sagashite...
- Kenseiden
- Lord of the Sword
- Maze Hunter 3-D / Maze Walker
- Megumi Rescue
- Miracle Warriors / Haja no Fuuin
- Nekkyuu Koushien
- Out Run
- Parlour Games / Family Games
- Penguin Land
- Phantasy Star (Japanese version only)
- Power Strike / Aleste
- R-Type
- Shinobi
- Solomon no Kagi
- Space Harrier 3-D
- SpellCaster / Kujaku Ou
- Super Racing
- Tensai Bakabon
- Thunder Blade
- Wonder Boy in Monster Land / Super Wonder Boy Monster World
- Ys (Japanese version only)
- Zaxxon 3-D
- Zillion II: The Tri Formation

The international versions of Phantasy Star and Ys removed the FM code to make
room for the translated text.

## Unofficial support

Released outside Japan: the FM unit is used when it is detected, but this was
never advertised (common Sega sound driver).

- Altered Beast
- California Games
- Casino Games
- Galaxy Force
- Game Box Serie Esportes Radicais
- Golfamania
- Monica no Castelo do Dragao
- Out Run 3-D
- Poseidon Wars 3-D
- Rampage
- Rastan
- Rescue Mission
- Scramble Spirits
- Shanghai
- Summer Games
- Tennis Ace
- Time Soldiers
- Turma da Monica em: O Resgate - Japan region only (`jap`)
- Ultima IV
- Vigilante
- Wonder Boy III: The Dragon's Trap - Japan region only (`jap`)

"Japan region only": these games check that the console is Japanese before
using the FM unit; start them with the Japan region (60 Hz).

Many games play their music on both the PSG and the FM unit and let the
console select one (port $F2). The Japan region emulates that selection: with
"FM only" the PSG is muted, as on a Japanese Master System. In the Europe /
USA regions both chips are heard together (FM unit plugged on an export
console), which can sound like PSG music with FM drums.

## Tested on the A4000 / ZZ9000

| Game | Region | FM | Result |
| --- | --- | --- | --- |
| Rastan | eur | yes | 50 FPS, FM music |
| Golvellius | jap | yes | 59.8 FPS, 0 drop, FM music |
| Wonder Boy III: The Dragon's Trap | jap | yes | 59.8 FPS (AHI and Paula), FM music |
| Wonder Boy III: The Dragon's Trap | eur / usa | no (export console) | PSG |
