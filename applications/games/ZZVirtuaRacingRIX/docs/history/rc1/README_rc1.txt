ZZPicoDrive32X rc1  -  Sega 32X sur le Core1 de la ZZ9000 (A4000/060, XX19d-RIX)
================================================================================

Lignee : ZZVirtuaRacingRIX A1 (blob a1b3 valide le 25.10 : 795 s, 3 courses de
Virtua Racing Deluxe, 47 670 PicoFrame a 60/s, 8,5 ms/image en moyenne, 0 image
> 16,67 ms, sortie propre avec une pile de 100 Ko).  rc1 = ce blob + deux
changements cote blob, et le launcher 68k complet.

Contenu
-------
  bin/zz32x_rc1.bin          blob Core1, SH-2 recompiles (CELUI A UTILISER)
                             MD5 735553a0b1317744953d76b7fe645919, 717692 octets
  bin/zz32x_rc1_interp.bin   meme blob, interpreteur SH-2 (reference, lent)
  bin/previous_A1b3/         les blobs A1 valides (zzvr_a1b3 / a1a3 et anterieurs)
  amiga/ZZPicoDrive32X.c     launcher 68k (source 1.1 public + A1diag + 32X)
  amiga/blob_data_stub.c     remplacant de blob_data.c (aucun blob embarque)
  Core1/, third_party/       sources completes du blob (regime PicoDrive)
  build_arm.sh               reconstruction du blob (ZZVR_DRC=1 ./build_arm.sh)
  CHANGELOG.txt, THIRD_PARTY_NOTICES.txt, SHA256SUMS.txt, docs/

Ce que change rc1 (une variable cote blob, le reste cote 68k)
-------------------------------------------------------------
Blob :
- chaque image est dessinee ; seules la conversion P96 et la publication
  attendent un buffer libre, decision prise en fin de PicoFrame.  En 1.1 le
  DESSIN attendait que le 68k ait pris l'image precedente ; l'horloge audio et
  le vblank RTG n'etant pas en phase, une image sur deux etait dessinee (29,9
  fps affiches sur la session A1).  Slot 176 = 1 (flag "gate") = comportement 1.1.
- buffer de sauvegarde 1,5 Mio (un savestate 32X fait ~680 Kio).
Launcher :
- pile de 100 Ko installee par le programme (StackSwap).  Le "__stack = 100000"
  du source 1.1 n'est pas honore par le build bebbo : le launcher 1.1 tourne sur
  la pile du Shell (4 Ko mesures), CloseWindow la fait deborder a la sortie et
  ecrase la memoire en dessous.  Tous les Guru/gels de sortie et le gel en
  course de A1 venaient de la.  Plus besoin de "stack 100000" dans le Shell.
- cadencement sur le vblank de la ZZ9000 (registre 0x4C) par PLL logicielle :
  sommeil timer.device jusqu'a 0,8 ms avant le front prevu, scrutation courte,
  reverrouillage a chaque front ; l'ack du PAN est vu dans la meme periode ->
  un flip par vblank RTG.  Flag "tof" = cadencement 1.1 sur WaitTOF (A/B).
- Del = reset console (commande unique, acquittee par le blob).
- pave 4/5/6/0 = X/Y/Z/MODE ; flag "noosd" ; config PROGDIR:ZZPicoDrive32X.cfg ;
  blob par defaut zz32x_rc1.bin ; instrumentation A1diag conservee (SNAP a la
  sortie, WATCHDOG, etapes EXIT, STACK).

Compilation du launcher (bebbo, shell MSYS, depuis amiga/)
----------------------------------------------------------
  m68k-amigaos-gcc -O2 -noixemul -m68020 -Wno-pointer-sign -o ZZPicoDrive32X ZZPicoDrive32X.c blob_data_stub.c -lamiga

(Pour une version finale : blob_data.c = bin2c de zz32x_rc1.bin, a la place du stub.)
Le source passe le controle de syntaxe/formats avec un gcc hote (stub NDK,
long 32 bits), les accolades et parentheses sont equilibrees ; pas compile avec
bebbo ici.  Si une ligne refuse, me la renvoyer.

Compilation du blob (optionnelle, bit a bit avec la meme chaine)
----------------------------------------------------------------
  ZZVR_DRC=1 ./build_arm.sh      -> bin/zz32x_rc1.bin

Protocole de test (NTSC, une variable par run)
----------------------------------------------
Run 1 - Virtua Racing, plus de "stack" a taper :
  ZZPicoDrive32X VR.32X zz32x_rc1.bin usa paula kbd
  Attendu au lancement : "STACK [start] ... free=97xxx", "Draw mode: every
  frame...", "pacing_mode=ZZ9000_VBLANK_PLL (vblank probe=OK, period=16667 us)".
  Une course, Shift+Esc.  A lire : la ligne "flips=... PicoFrames=..." (les
  deux doivent etre proches : 60 affichees), "FPS=59.x", la ligne "PLL ZZ9000:
  ... missed=0 late=~0 ack ... notseen=~0", le bloc NTSC_TIMING_DIAG (le dessin
  de chaque image coute un peu plus : moyenne attendue ~9,5 ms), "Done (rc=0)".
  Si l'image semble moins fluide qu'avant ou si "notseen" est grand : refaire
  le meme run avec le flag "tof" en plus (A/B launcher), puis avec "gate" (A/B
  blob), et me donner les trois lignes "flips=" / "PLL".
Run 2 - un autre jeu NTSC (Doom, Knuckles Chaotix, Star Wars Arcade, After
  Burner, Space Harrier, Virtua Fighter...) : meme commande avec sa ROM.
  Le launcher accepte .32x et .bin.
Run 3 - pendant une partie : Shift+F1 (sauvegarde slot 1), jouer, F1
  (rechargement) ; Del (reset) ; puis Shift+Esc.  Attendu : "STATE: sauve slot 1
  (~700000 octets)", "RESET requested" / "RESET done".
Touches clavier (flag kbd) : fleches, pave 1/2/3 = A/B/C, pave 4/5/6 = X/Y/Z,
pave 0 = MODE, Entree = Start, Del = reset, Shift+Esc = quitter.

Limites connues rc1
-------------------
- Un seul jeu valide sur materiel (Virtua Racing Deluxe USA) ; les autres sont
  a tester.  PAL/EUR non vise par rc1.
- GUI 1.1 conserve mais pas re-teste ; la ligne OSD du blob est allumee par
  defaut (flag noosd) : la version finale l'eteindra.
- Le gel/plantage de sortie 1.1 vaut aussi pour ZZPicoDriveMD/SMS 1.1 publics
  (meme "__stack" inoperant) : a corriger la-bas de la meme facon.

Credits : launcher et sources Core1 ZZPicoDriveMD par Xanxi (2026) ; PicoDrive
par notaz / irixxxx, Cyclone 68000 par Dave Haywood / notaz / irixxxx, hooks
PicoDrive de VirtuaRacingDeluxeEnhanced par PokaMilkTea (THIRD_PARTY_NOTICES.txt).
Aucune ROM ni donnee derivee de ROM dans ce paquet.  Usage non commercial.
