ZZVirtuaRacingRIX A1b3 / A1a3  -  Sega 32X (PicoDrive) sur le Core1 de la ZZ9000
================================================================================

A1b3 (25.10, 12h30) = A1b2 + acquittement immediat de STOP depuis les boucles
de cadencement + diag "convert" corrige.  Emulation inchangee.  Blobs :
bin/zzvr_a1b3.bin (DRC, celui a utiliser) et bin/zzvr_a1a3.bin (interpreteur).
Commande :  ZZPicoDriveMD "Virtua Racing Deluxe (USA).32x" zzvr_a1b3.bin usa ahi


A1a2 (25.10, 12h) = A1a + UNE variable : la section de pile du Core1 (0x05F,
SP firmware = 0x06000000) passe de Strongly Ordered a cache WB/WA, comme dans
le blob ZZQuake release.  Blobs : bin/zzvr_a1a2.bin et bin/zzvr_a1b2.bin ;
les A1a/A1b d'origine sont conserves dans bin/previous_A1a/.  Commande de test :

  ZZPicoDriveMD "Virtua Racing Deluxe (USA).32x" zzvr_a1a2.bin usa ahi

Ligne OSD : le premier champ devient A1A2 / A1B2.  Nouveau champ partage 198 =
SP vu a l'entree (attendu 0x05FFFFxx : si ce n'est pas le cas, le mapping ne
sert a rien et il faudra chercher ailleurs).

Le reste de ce fichier decrit A1a/A1b et reste valable (lire zzvr_a1a2 pour
zzvr_a1a, zzvr_a1b2 pour zzvr_a1b).


Etat : compile et lie (arm-none-eabi-gcc 13.2.1 20231009), verifications
statiques faites.  RIEN N'A ENCORE TOURNE SUR UNE ZZ9000.  Les deux blobs sont
des builds de mesure, pas des versions jouables.

Objet de A1
-----------
Repondre a une seule question : PicoFrame() avec le son et les deux SH-2 actifs,
pendant une vraie course de Virtua Racing Deluxe, tient-il dans le budget NTSC
(16,67 ms par image emulee) ?  Cible : <= 12 ms avant d'ajouter le HLE et
RasterIX ; > 15 ms = arret du developpement graphique, recherche d'optimisations
CPU ; irrealisable = NO-GO 32X.

  A1a  zzvr_a1a.bin  32X avec l'interpreteur SH-2.  Etape de validation du
                     demarrage 32X seulement (logo SEGA/32X, titre, course).
                     Sa vitesse n'est pas l'objectif.
  A1b  zzvr_a1b.bin  32X avec le recompilateur SH-2 ARM (DRC).  C'est lui qui
                     donne la mesure GO/NO-GO.  A lancer SEULEMENT quand A1a a
                     atteint une course sur l'A4000 (une variable par essai).

Aucune version publique n'est modifiee : ZZPicoDriveMD 1.1 (executable 68k et
blob) reste tel quel ; il sert de launcher a ces deux blobs.

Contenu du paquet
-----------------
  README_A1a.txt                ce fichier
  CHANGELOG.txt                 ce qui change par rapport a ZZPicoDriveMD 1.1
  THIRD_PARTY_NOTICES.txt       licences et provenance (PicoDrive, Cyclone, VRDE)
  build_arm.sh                  construction des deux blobs (voir "Compilation")
  bin/zzvr_a1a.bin  (+ .md5, .sha256)
  bin/zzvr_a1b.bin  (+ .md5, .sha256)
  Core1/                        couche ZZ du blob (zzvr_*.c/.h/.S/.ld, stubs/)
  third_party/picodrive/        PicoDrive irixxxx 8b5e91bc tel que livre par
                                VirtuaRacingDeluxeEnhanced 1.2.0 (hooks "VRDE:"),
                                + cpu/cyclone/ (Cyclone 68000, irixxxx 3ac7cf1b,
                                l'archive de ZZPicoDriveMD 1.1, + Cyclone.s pre-genere,
                                md5 ff097962906336cf7879c22ff42bcc18), + tools/mkoffsets.sh
                                (amont), + UN correctif garde dans cpu/drc/emit_arm.c
                                (ZZVR_FAR_CALLS, voir A1b)
  third_party/LICENSE_VRDE.txt  licence des modifications VRDE de PicoDrive
  docs/ZZVIRTUARACINGRIX_A0_AUDIT.md         rapport d'audit A0
  docs/diffs_vs_ZZPicoDriveMD_1.1/*.diff     diffs exacts des fichiers ZZ et du
                                             script de build par rapport a 1.1,
                                             et du seul fichier PicoDrive touche
  SHA256SUMS.txt                somme de chaque fichier du paquet

Carte memoire du blob (zzvr_mem.h, zzvr_shared.h)
--------------------------------------------------
Zones Zorro XACP, INCHANGEES par rapport a ZZPicoDriveMD 1.1 (le launcher 1.1
les utilise telles quelles) :
  0x04500000  blob (fenetre 2 Mio, ASSERT du .ld a 0x046F0000)   REG_ARM_RUN 0x0450/0x0000
  0x04700000  shared NC (index 0..175 identiques a 1.1 ; 180..199 ajoutes, lecture seule)
  0x04B00000  ROM (7 Mio, limite blob 4 Mio ; Virtua Racing Deluxe = 3 Mio)
  0x05600000  video FB3A (320x240 RGB565 produit par PicoDrive), 0x05700000 audio NC,
  0x05800000  debug, 0x07E00000 buffer de sauvegarde (acces identiques a 1.1)
Memoire privee de l'application : UNIQUEMENT l'arene Core1 XACP v1.7
0x30000000..0x3F800000, mappee WB/WA par le blob (modele ZZQuake2RIX/ZZOpenLaraRIX).
Plus rien dans 0x22000000..0x30000000 (pool v1.6 / ZZMIDI) :
  0x30000000  BSS        16 Mio   (.bss, zero a l'entree ; A1a 1,2 Mio, A1b 5,5 Mio)
  0x31000000  tas _sbrk  16 Mio   (malloc du coeur ; tables du DRC ~5 Mio en A1b)
  0x32000000  pool mmap  32 Mio   (plat_mmap : Pico32xMem ~0,6/0,85 Mio)
  0x34000000  cache DRC   8 Mio   (4 Mio utilises, A1b)
  0x3D800000  reserve RasterIX (A3, placements ZZQuake2RIX ; non utilise ici)
  0x3F800000  garde firmware, jamais mappee ni touchee
Bornes verifiees a la compilation (zzvr_mem.h) et a l'edition de liens (zzvr.ld).
Toutes ces adresses sont < 0x80000000 : les tables memoire SH-2 de PicoDrive
(pointeur >> 1, bit 31 = handler) restent valides pour la ROM, la SDRAM/DRAM
(dans le pool mmap) et le cache DRC.

Compilation
-----------
Pre-requis : bash et arm-none-eabi-gcc 13.2.1 seulement (Arm GNU Toolchain
13.2.Rel1 ou xPack 13.2.1-1.1 : meme GCC 13.2.1 20231009).  Cyclone.s est livre
pre-genere (sortie deterministe du generateur Cyclone) : pas de compilateur natif
ni de make necessaires.  Tout est relatif au dossier du script ; aucune
conversion de chemin.  Ne pas convertir les fins de ligne des .sh en CRLF.

Windows / MSYS2 (shell MSYS2 ou MINGW64), chaine dans C:\ArmGNUToolchain\13.2 Rel1 :
  cd /c/Softwares/ZZ9000/Audio_project/ZZVirtuaRacingRIX/ZZVirtuaRacingRIX_A1a
  ./build_arm.sh                    -> bin/zzvr_a1a.bin   (A1a)
  ZZVR_DRC=1 ./build_arm.sh         -> bin/zzvr_a1b.bin   (A1b)
Le script trouve "/c/ArmGNUToolchain/13.2 Rel1/bin" tout seul ; pour une autre
installation : TOOLCHAIN_BIN="/c/chemin/vers/bin" ./build_arm.sh
Si le shell affiche "file: command not found" pendant la premiere etape, c'est
le script amont mkoffsets.sh qui tente la commande file : sans effet, il passe
par arm-none-eabi-objdump.

Conteneur Debian (chaine dans PATH) :
  cd ZZVirtuaRacingRIX_A1a && ./build_arm.sh && ZZVR_DRC=1 ./build_arm.sh

Le build dure environ une minute.  Sortie attendue (A1a) :
  .text 393408 / .data 264120 / .bss 1223712 (a 0x30000000)
  BIN: 657528 bytes -> bin/zzvr_a1a.bin
  md5 d07ab6a32f179add8b2d217c36741aa3
Un build de A1b apres A1a (ou l'inverse) recompile tout : les objets vont dans
build/ et pico_int_offs.h est regenere a chaque fois (sa taille de Pico32xMem
depend de DRC_SH2).

Les .md5/.sha256 sont ecrits a cote des .bin.  Sommes des blobs livres :
  zzvr_a1a.bin  657528 octets  md5 d07ab6a32f179add8b2d217c36741aa3
  zzvr_a1b.bin  717500 octets  md5 3db33ef26e762c115b9a18bf8e9d7967
(voir bin/*.sha256 pour les SHA-256).

Deploiement et test sur l'A4000 (relances a chaud, pas de cycle d'alimentation)
-------------------------------------------------------------------------------
1. Copier bin/zzvr_a1a.bin dans le repertoire du ZZPicoDriveMD 1.1 public.
2. Depuis un Shell (pour lire les statistiques de fin), avec la ROM
   "Virtua Racing Deluxe (USA).32x" (SHA-1 18dfdeb50780c2623e60a6587d7ed701a1cf81f1,
   la seule connue du HLE) :

     ZZPicoDriveMD "Virtua Racing Deluxe (USA).32x" zzvr_a1a.bin usa ahi

   Le launcher 1.1 charge un blob externe quand il est present (argument 2) ;
   "usa" force la region NTSC 60 Hz (budget 16,67 ms) ; "ahi" = son AHI.
   Les options 1.1 (kbd/cd32/db9, paula, silence, 2p...) restent valables.
3. Sequence attendue : ecran noir court, logo SEGA (couche 32X run-length),
   logo 32X, titre, demo, course (START puis choix).  Lent, c'est normal pour A1a.
4. Lire la ligne OSD en haut de l'image (8 lignes de la bordure noire, voir
   ci-dessous), photographier pendant une course, puis quitter normalement :
   le launcher 1.1 imprime a la sortie "PicoFrame blob (N frames) min= moy= max=
   ms" et les compteurs >12 / >14 / >16,67 ms sur TOUTE la session.
5. Si A1a atteint une course : meme protocole avec zzvr_a1b.bin (A1b).

Ce qu'il faut me rapporter : la sortie Shell complete, une photo de l'OSD en
course (A1a et A1b), le comportement du son, tout code ERROR/DIAG ou reset loop.

Ligne OSD (dessinee par le blob, aucune modification 68k)
---------------------------------------------------------
  A1A 12.3 11.9 15.2 X1 06001234 06000ABC
  |   |    |    |    |  |        slave SH-2 pc
  |   |    |    |    |  master SH-2 pc
  |   |    |    |    X1 = adaptateur 32X actif (X0 = pas encore / ROM MD)
  |   |    |    PicoFrame max sur les 600 dernieres images (10 s), ms
  |   |    PicoFrame moyen sur les 600 dernieres images, ms   <- LE chiffre GO/NO-GO
  |   PicoFrame de la derniere image, ms
  A1A ou A1B (interpreteur / DRC)
La fenetre de 600 images contient les 10 dernieres secondes : lue en course,
elle mesure la course (le total de session imprime a la sortie inclut les menus).
PicoFrame = emulation complete d'une image (68000, Z80, YM2612/PSG, VDP, deux
SH-2, PWM, rendu PicoDrive dans FB3A) ; la conversion P96 et le dcache clean sont
mesures a part (champs 1.1, imprimes a la sortie).  Avec skipFrame=1 (le 68k
n'a pas encore repris l'image precedente) PicoFrame ne dessine pas : le compte
d'images dessinees est publie (index 197) pour ponderer si besoin.
Si l'image est en mode 240 lignes (pas de bordure), la ligne couvre les 8
premieres lignes de l'image : acceptable pour un build de mesure.

Champs partages ajoutes (adresse ARM 0x04700000 + index*4, lecture 68k via rd32)
--------------------------------------------------------------------------------
  180 ZZVR_SH_MAGIC      0x5A565231 'ZVR1'      181 ZZVR_SH_BUILD  0x0A1A / 0x0A1B
  182 AHW (PicoIn.AHW, bit 0x04 = 32X)           183 Pico32x.regs[0] (ADEN bit0)
  184 master SH-2 pc                              185 slave SH-2 pc
  186 plat_mmap octets                            187 _sbrk octets
  188 PicoFrame moyen fenetre (us)                189 PicoFrame max fenetre (us)
  190 PicoFrame derniere image (us)               191 VFP actif (FPEXC.EN relu)
  192 DRC actif                                   193 refus plat_mmap (doit rester 0)
  194 Pico32x.vdp_regs[0]                         195 68k->Core1 : 1 = OSD off
  196 images dans la fenetre                      197 dont dessinees
Les champs 1.1 (156..167 : min/moy/max/histogramme PicoFrame de session) sont
toujours renseignes ; le launcher 1.1 les imprime.

A1b : recompilateur SH-2 (ne pas lancer avant la validation de A1a)
--------------------------------------------------------------------
- POPT_EN_DRC + cpu/sh2/compiler.c (-DDRC_SH2), cache de code dans l'arene
  (0x34000000, plat_mem_get_for_drc), tables du DRC par malloc (tas 0x31000000),
  cache_flush_d_inval_i = DCCMVAU + ICIMVAU + BPIALL + barrieres (zzvr_mmu.c).
- L'emetteur ARM de PicoDrive n'emet que des BL directs (portee +-32 Mio) et
  quitte sur une cible hors portee ("should never happen").  Le code du blob est
  a 0x04500000, le cache en arene a 0x34000000 : hors portee.  Correctif livre,
  garde par ZZVR_FAR_CALLS (docs/diffs.../picodrive_emit_arm.c.diff) : appel
  indirect par litteral (add lr,pc,#8 ; ldr pc,[pc] ; mov pc,pc ; .word cible),
  la sequence du port Symbian de PicoDrive.  Cout : quelques cycles par appel
  du code recompile vers les handlers C (acces memoire non inlines, sync).  Si
  A1b tombe dans la zone grise 12-15 ms, l'alternative sans surcout est un petit
  chargeur qui recopie le blob dans l'arene au demarrage (code et cache alors a
  portee de BL) : a arbitrer a ce moment-la, pas avant.
- Au premier demarrage le DRC teste l'execution de code dans son cache
  (drc_cmn_init) ; un echec se verrait comme un reset loop Core1 immediat
  apres le lancement du 32X (DIAG 0xA132 atteint, puis plus rien).
- Un debordement de table du DRC (printf + exit) bloque le Core1 : watchdog
  "CORE1 FIGE" du launcher, pas de corruption.

Limites connues de A1
---------------------
- Sauvegardes d'etat : un etat 32X depasse les 512 Kio du buffer d'echange 1.1
  (ZZPICO_SAVE_MAX) ; ne pas utiliser SAVE/LOAD STATE en A1 (la SRAM va bien).
- Seule la ROM USA est visee (le HLE de A2 n'en connait pas d'autre) ; une autre
  ROM 32X demarre mais n'est pas le sujet.
- Manette : port 1 en 6 boutons (X/Y/Z = vues, comme la vraie console) ; port 2
  en 3 boutons.  Le launcher 1.1 transmet deja X/Y/Z/MODE.
- Pas de HLE, pas de RasterIX, pas d'Emu68-A9 : hors perimetre A1.

Constat annexe sur ZZPicoDriveMD 1.1 (a traiter a part, sans urgence)
----------------------------------------------------------------------
L'archive PicoDrive 26ecb2b6 publiee avec ZZPicoDriveMD 1.1 ne compile pas
telle quelle sous -DNO_32X (pico/state.c : Pico32xStateLoaded(1) contre une
macro sans argument, p32x_event_times non declare) : le build 1.1 avait un
correctif local non archive.  Avec un correctif minimal la chaine lie, mais le
binaire (602312 o) differe du blob public (598968 o).  Sans incidence sur A1
(arbre PicoDrive different), mais le paquet source public n'est pas reproductible.
