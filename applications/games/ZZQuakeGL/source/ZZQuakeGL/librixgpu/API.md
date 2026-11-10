# librixgpu 1.1a — pilote Core1 du RasterIX G4B (ZZ9000)

Version `RIXGPU_VERSION = 0x00010002` (`rix_version()`). Cinq modules, aucun n'appelle Core0, le
firmware ou le 68k ; tout passe par la FIFO AXI-Stream (ST0→ST1) et des listes en DDR lues par le
FrameStreamingCore (MEM→ST1). Lignée : ZZCube B1d (6 runs) + ZZGear C1c (6 runs) + ZZGear D1/D1a
(2 runs), G4B + FAST1 intacts. **Ce qui est nouveau en 1.1 (D2) a été compilé, simulé et testé sur
hôte ; rien de nouveau n'a encore tourné sur l'Amiga** — voir BILAN.md pour la frontière exacte.

| module | rôle |
|---|---|
| `rixgpu.[ch]` | cœur : timer global, WFI, FIFO (erreurs d'un paquet précédent observées avant chaque envoi), échelle d'init (L0…L15), listes et registres, fences, PL330 swap, VDMA, sondes de capacités (9) et de placement, `rix_hw_caps()` |
| `rixframe.[ch]` | image = anneau de morceaux de listes DDR avec fence par morceau ; états en flux ; `rix_frame_clear` (memsets en plein flux) ; bind de texture hors des morceaux (paquet FIFO ou courte liste DDR, fence de bind, règle 11) et suivi de la texture chargée dans la TMU ; fence d'image ; `rix_frame_diag_last` |
| `rixtex.[ch]` | pool de pages de 2 Kio (jusqu'à 16 Mio) n'importe où en DDR, objets texture (≤ 256×256, RGB565), uploads RGB565 / RGB888 / palettisés 8 bits, sous-rectangles, nettoyage de cache |
| `rixdesc.[ch]` | descripteur triangle 39 mots (bit-exact A07b pour couleur/arêtes/texture, profondeur z/w remplie) |
| `rixclip.[ch]` | clipping des triangles : plan near en espace clip, bords de l'écran en espace écran, éventail de descripteurs |
| `rix_hw.h` | encodages matériels (registres, bits de features, pipeline fragment, config TMU, env TMU REPLACE/MODULATE, descripteur) |
| `rix_diag.h` | contrat de diagnostic : drapeaux `RIXF_*`, capacités `RIXCAP_*`, statistiques `RS_*`, auto-test `SD_*`, échelle `LAD_*`, erreurs `RIXE_*` |
| `rix_window.h` | disposition de la fenêtre G4B de 2 Mio utilisée par l'échelle d'init et les démos, rectangle de clipping `RIX_CLIP_*` |

La bibliothèque ne dépend plus de la mailbox des démos : l'application lui donne ses zones de
diagnostic (`rix_config.stats`, `.sd`) et 64 octets non cacheables pour le programme PL330
(`.dma_prog`). `rix_shared.h` (ABI mailbox 68k↔Core1) appartient maintenant à l'application ZZGear.

Lien : `librixgpu.a` (arm-none-eabi 13.2, Cortex-A9, NEON, softfp, `-ffreestanding`), 72 Kio,
82 fonctions exportées, aucune dépendance autre que libm/libc (sinf/cosf absents : pas utilisés
par la bibliothèque). L'application fournit `dcache_clean_range()` (zzcore_runtime.c) et, si elle
veut WFI, les mappings MMU du périmètre (zzgear_mmu.c sert de modèle).

## Règles matérielles codées dans la bibliothèque (toutes mesurées sur G4B)
1. **Masques avant memset** : un memset couleur avec le masque couleur (REG 3 bits 15–18) à 0 n'écrit rien et **fige le parser** (B1 run 1) ; un memset depth avec le masque depth (bit 14) à 0 n'écrit rien (C1b). `rix_dl_clear()` / `rix_dl_clear_depth()` réaffirment le masque.
2. **Table de pages de texture uniquement par la FIFO** : dans une liste DDR elle bloque FSC/TMU sur le canal de lecture AXI (B1 run 5). `rix_frame_texture()` ferme le morceau, envoie config + table en paquet FIFO (ordonné derrière le morceau par le FSC), rouvre un morceau.
3. **Jamais de COMMIT ni READ du depth buffer** (jamais acquittés par RasterIXCoreEF.v) : seul MEMSET.
4. **Pas de reset GPU logiciel** sur G4B : un paquet jamais consommé = GPU figé jusqu'au cycle d'alimentation ; l'échelle et les sondes envoient une fonction par paquet et nomment le coupable (E1A0, E1Bk, E1Cx).
5. **Fence** = memset scissoré de 64 pixels dans la ligne 480 (jamais balayée) du buffer couleur, précédé de 64 NOP en mode DDR ; une liste DDR n'est jamais réécrite avant sa fence.
6. **Convention y** : RasterIX range le pixel à `((yres-1)-y)*xres+x` : y croît vers le haut ; la ligne y = 0 (ligne mémoire 480) porte les fences, le rectangle dessinable est `RIX_CLIP_*` = [0, 639,97) × [1, 480,97).
7. **Textures** : RGB565 little-endian, lignes contiguës, pages de 2 Kio en ordre ; **la TMU n'a pas de mémoire de texture externe** : chaque `OP_TEXTURE` recopie la texture dans la BRAM du TMU (64×64 ≈ 16 µs, 256×256 ≈ 233 µs voie FIFO, HP3 ≈ 800 Mo/s). Une TMU (descripteur de 39 mots = `TMU_COUNT 1`). Le MMIO FIFO coûte ≈ 1,1 µs par mot.
8. **Mémoire** : seul le buffer couleur balayé doit vivre dans la fenêtre 68k ; listes, textures, Z peuvent vivre ailleurs en DDR (sonde de placement : arène Core1 0x30000000) à condition que Core1 nettoie son cache après écriture (`rix_mem_clean`, fait par `rix_tex_write_*` quand le pool est déclaré cacheable, et par `rixframe` quand `ring_cacheable` est mis). **`rix_mem_clean` est un nettoyage L1 par adresse (CP15 DCCMVAC) : il n'atteint pas le L2 partagé (PL310).** La zone lue par le GPU doit donc être mappée Non-Cacheable, ou *inner write-back / outer non-cacheable* (`zzgear_mmu.c`, D2 : TEX=100, B=1) ; une zone outer write-back ne serait vue par le GPU qu'après le flush L2 de Core0 au vblank (revue D2). Les buffers couleur sont lus par Core1 (fences) : Non-Cacheable obligatoire.
9. **Réutilisation d'un buffer couleur** : `rix_frame_begin()` sur un buffer suppose que la fence de l'image précédente sur ce buffer a été attendue (tous ses morceaux sont finis) : il libère les slots d'anneau qui le visaient et remet à zéro ses pixels de fence de morceau. Une fence « passée » = pixel ≠ 0 et séquence dans une fenêtre de 1 024 au-delà de la valeur attendue (plus de test signé sur 32 768 : un pixel périmé ne peut plus passer pour neuf).
11. **Jamais de liste DDR derrière un chargement de texture non exécuté, ni d'`OP_TEXTURE` au milieu d'un long morceau — G4B et G4C (B1 run 5, D2 run 12, D2e 14/10)** : le FrameStreamingCore sert à la fois les lectures de listes (MEM→ST1) et les lectures de pages de texture. Si une lecture de liste est en cours — et bloquée par un parser occupé — quand le parser exécute `OP_TEXTURE`, les pages ne peuvent plus être lues : deadlock, cycle d'alimentation. Deux formes : (a) une liste soumise derrière un bind dont la fence n'est pas passée (D2 run 12) ; (b) un `OP_TEXTURE` à l'intérieur d'un morceau que le FSC n'a pas fini de lire (**D2e, G4C : première image de ROOM A**). L'étape L15 (table de pages dans une liste DDR) passe sur G4C uniquement parce que sa liste est courte (144 mots, 77 après la table : entièrement préchargée avant que le parser l'atteigne) ; le crédit de lecture C1 ne change rien au mécanisme. `rix_frame_texture()` ferme donc le morceau, envoie config + table + **fence de bind** (pixels 128..191 de la ligne 480) hors des morceaux — **`RIX_BIND_FIFO`** (défaut : paquet FIFO, jusqu'à 80 mots MMIO) ou **`RIX_BIND_LIST`** (1.2.1 : les mêmes mots comme liste DDR à part de 80 mots au plus, soumise avec 2 mots FIFO ; `rix_frame_set_bind_mode`, exige `rix_ring_cfg.bind_base` et `RIXCAP_TEX_DDR`, E125 sinon) — et `chunk_close()` ne soumet le morceau DDR suivant qu'une fois la fence de bind passée (`RIXE_BIND_TIMEOUT` E129 sinon) ; Core1 construit le morceau pendant l'attente, seule la soumission attend (`rix_frame.bind_wait_us`). Le simulateur modélise les deux formes (préchargement : 77 mots après la table, la valeur prouvée par L15) et refuse le motif. `RIXF_BINDFIFO` = pas de L15 à l'init (obligatoire sur G4B où L15 fige le GPU).
12. **(G4B seulement) Listes DDR = écritures seulement (D2c run 23, 12/10) — levée sur G4C C1 (14/10 : sondes 9–13 WORKS, ROOM en DDR)** : la sonde 9 — deux triangles plats avec test de profondeur dans une liste DDR de 130 mots — fige le parser ; le même contenu par la FIFO (sonde 0) fonctionne, et la scène ROOM entière se rend en transport FIFO. Toutes les listes DDR validées depuis B1 ne portaient que des écritures (memsets couleur/profondeur, triangles opaques sans test). Mécanisme supposé (non prouvé dans le RTL) : la lecture de Z par fragment partage le canal de lecture AXI avec la lecture de liste du FSC — le deadlock de la règle 11 par fragment, donc sans fence possible ; l'hypothèse « liste courte entièrement préchargée » est exclue (130 mots < FIFO). Blending seul en liste DDR : non testé (la série de sondes s'arrête au premier figeage). **Conséquence : un morceau DDR ne contient ni test de profondeur ni blending** ; pour ces contenus, transport FIFO — par le PL330 (transport 3, ci-dessous) pour ne pas payer 1,1 µs par mot.
13. **Transport 3 — FIFO ST0 alimentée par le PL330 (`RIXF_DLDMA`, 1.1d)** : `rix_submit()` découpe le morceau en paquets de 248 mots (frontières de commandes, deux paquets tiennent dans la FIFO de 512), écrit l'en-tête FSC (2 mots) par MMIO, lance le DMAC (canal 6, programme à `dma_prog + 0x200`, source = la liste en DDR non cacheable, destination fixe TDFD, rafales FIXED de 16 beats ou stores unitaires selon l'auto-test), attend la fin du DMA, écrit TLR ; la vacance est attendue avant chaque paquet comme en FIFO CPU, les erreurs FIFO (TPOE/TSE) sont vues au paquet suivant. Le GPU ne lit aucune liste DDR dans ce mode. L'auto-test `rix_dmafifo_selftest()` (comptage sans TLR puis reset de la FIFO, intégrité par une fence portée par le DMA, débit sur 8 × 248 NOP) décide du mode et renseigne `SD_DMAFIFO_*` ; `RIXCAP_DMAFIFO` ; repli FIFO CPU sinon (`RIXE_DMAFIFO` E132 dans `SD_INIT_FIRST_ERROR` seulement si le GPU a figé). Le paquet de bind reste en MMIO (85 mots au plus, et sa source est en BSS cacheable).
10. **Uploads pendant qu'une image est en vol** : la TMU recopie la texture à l'exécution du bind, pas à la soumission. Réécrire une texture que l'image N (soumise, non fencée) va encore binder fait lire à N les texels de N+1 ; sans gravité pour une lightmap (une image d'avance), à proscrire pour un remplacement de texture (attendre la fence ou doubler la texture).

## Cycle de vie
```c
static volatile u32 stats[RS_COUNT], sd[SD_COUNT];       /* où l'application veut (mailbox ou pas) */
rix_config cfg = { 0 };
cfg.flags = 0; cfg.idle_tick_us = 100;
cfg.stats = stats; cfg.sd = sd; cfg.caps_out = &caps; cfg.timer_hz_out = &hz;
cfg.dma_prog = NC_64_BYTES;                              /* programme PL330, DDR non cacheable */
cfg.probe_tri = desc39; cfg.probe_x = 320; cfg.probe_y = 250;   /* étapes texturées de l'échelle (optionnel) */
cfg.tmu_env = RIX_TMU_ENV_REPLACE; cfg.tmu_env_color = 0; cfg.tmu_cfg = tex.cfg; cfg.tex_pages = tex.pages; cfg.tex_base = tex.base;
rc = rix_init(&cfg);                 /* échelle L0…L15, auto-tests DMA/NEON/WFI ; 0 ou RIXE_* ; RIXE_CONFIG si stats/sd/dma_prog manquent */
...                                   /* listes d'état initiales par rix_dl_* + rix_submit_fifo() */
rix_ring_cfg rc = { ring_base, slots, chunk_bytes, fence_x, color_bufs, n, ring_cacheable };
rix_ring_init(&rc);                   /* après la dernière liste d'init fencée */
rix_texpool_init(base, pages, cacheable);
rix_tex_alloc(&t, 256, 256, 1); rix_tex_write_rgb888(&t, rgb, 0);
rix_hwcaps hc; rix_hw_caps(&hc);      /* capacités effectives (après rix_run_probes si on veut depth/blend/... renseignés) */
...
rix_shutdown();
```

### Capacités effectives : `rix_hw_caps()`
`rix_hwcaps` = version d'API, identification matérielle (`hw_magic`, `hw_version`), bits
`RIXCAP_*` de l'échelle, puis pour chaque fonction un verdict `RIX_HWCAP_YES / NO / UNKNOWN`
(UNKNOWN tant que la sonde correspondante n'a pas tourné) : `ddr_lists`, `textures`,
`depth_test`, `blending`, `alpha_test`, `arena_placement`, `tmu_modulate`, `lightmap_blend` ;
coûts mesurés `tex_switch_64_us` / `tex_switch_256_us` ; constantes de la configuration G4B :
`tmu_count 1`, `tex_max_size 256`, `tex_page_bytes 2048`, `depth_bits 16`, `desc_words 39`,
`fifo_words`, `timer_hz`. Un renderer teste `hc.depth_test == RIX_HWCAP_YES` plutôt que de
supposer ; ce qui est NO est une limite du bitstream, ce qui est UNKNOWN une limite de la sonde.

## Une image (rixframe)
```c
rix_frame_desc d = { .color = FB, .zbuf = Z, .features = RIX_FEAT_TMU0 | RIX_FEAT_DEPTH_TEST,
                     .fragment = rix_fragment(RIX_TF_LEQUAL, 1, RIX_TF_ALWAYS, 0, RIX_BF_ONE, RIX_BF_ZERO),
                     .clear_rgba = 0, .clear_depth16 = 0xFFFF, .clear_color = 0, .clear_depth = 1,
                     .rect_x0 = 0, .rect_y0 = 0, .rect_x1 = 0, .rect_y1 = 0 };   /* 0 = plein écran */
rix_frame_begin(&f, &d);
rix_frame_reg(&f, RIX_REG_TMU0_ENV, RIX_TMU_ENV_REPLACE);
rix_frame_texture(&f, &tex);           /* bind TMU0 : ferme/rouvre un morceau ; SAUTÉ si la texture est déjà dans la TMU */
for (...) rix_clip_draw(tri, near, &vp, RIX_CLIP_X0, RIX_CLIP_Y0, RIX_CLIP_X1, RIX_CLIP_Y1, emit, &f, &n);   /* emit = rix_frame_tri */
rix_frame_fragment(&f, rix_fragment(RIX_TF_LEQUAL, 0, RIX_TF_ALWAYS, 0, RIX_BF_ZERO, RIX_BF_SRC_COLOR));  /* 2e passe lightmap */
rix_frame_features(&f, RIX_FEAT_TMU0 | RIX_FEAT_DEPTH_TEST | RIX_FEAT_BLENDING);
rix_frame_texture(&f, &atlas); ...
rix_frame_end(&f, fence16);            /* fence de morceau + fence d'image, soumission */
rix_frame_wait(FB, fence16, 1000000, &stop, &polls);
/* f.chunks, f.words, f.tris, f.ringwait_us, f.first_submit_tick, f.tex_binds, f.tex_skipped, f.tex_bind_us, f.bind_wait_us */
```
Les morceaux sont soumis dès qu'ils sont pleins (`chunk_bytes`, réserve de queue 200 mots) : le GPU
dessine pendant la construction. `rix_frame_scissor()` pose un scissor (x1 = 0 : off), restauré
avec REG 0/3 à chaque frontière de morceau (rollover, bind). Toute écriture de REG 0/3 passe par
`rix_frame_features/fragment` ; `rix_frame_reg` sert aux autres registres (env TMU, fog,
stencil…). Les fences de morceau et d'image sont des memsets exécutés avec les features du
pipeline à 0 (comme l'échelle et les sondes), le morceau suivant restaure l'état.

**Suivi de la texture chargée** : `rix_frame_texture()` ne renvoie rien à la TMU quand base,
pages et config sont ceux du dernier bind (compteur `tex_skipped`) ; un upload (`rix_tex_write_*`)
ou `rix_tex_touch()` invalide ce suivi, `rix_ring_reset()` aussi. Trier les primitives par texture
reste la seule façon de réduire les binds (une TMU, pas de cache de texture dans le FPGA).

## Clipping (rixclip)
`rix_clipvert { x, y, z, w, u, v, r, g, b, a }` en espace clip (après projection, avant la
division). `rix_clip_near(in, n, w_min, out)` coupe au plan w = w_min (interpolation linéaire exacte
en espace clip) ; `rix_project(in, n, &vp, out)` divise et applique le viewport
(`rix_viewport { scale_x, trans_x, scale_y, trans_y, scale_z, trans_z }`, y vers le haut) ;
`rix_clip_screen(in, n, x0, y0, x1, y1, out)` coupe aux quatre bords (u/w, v/w, 1/w, z linéaires
en espace écran) avec un chemin rapide quand tout est dedans ; `rix_clip_draw()` enchaîne les
trois et émet l'éventail de descripteurs par un callback. Un triangle donne au plus 4 sommets
après le near, 8 après l'écran (`RIX_CLIP_MAX 12`). Les intersections sont calculées dans un
ordre canonique (du sommet intérieur vers l'extérieur) pour que deux polygones partageant une
arête produisent le même sommet au bit près (sinon fissure de 1 px près du near). Validé sur hôte
par `test_clip` (2000 triangles aléatoires, 40 M pixels comparés à un rasteriseur de référence,
0 pixel hors tolérance) et par le test « pas de trou » de la scène ROOM dans le simulateur.

Le culling se fait avant le clipping (signe de `rix_tri_area()` sur les sommets projetés quand
tous sont devant le near, sinon normale en espace monde) ; ZZQuake2 fera du frustum culling par
BSP/PVS en amont, le clipper ne voit que les triangles candidats.

## Descripteur (rixdesc)
`rix_vertex { sx, sy, z, invw, u, v, r, g, b, a }` → `rix_tri_desc()` (0 si aire nulle ou bbox hors
640×481 : clipper avant). `z` = profondeur fenêtre 0..1 (0 = près), `invw` = 1/w ; u, v voyagent en
u/w, v/w (perspective correcte dans le FPGA). Prouvé égal mot à mot à `cube_make_desc` A07b
(host_tests/test_rix : 3600 triangles). `rix_tri_area()` donne l'aire signée (culling).

## Textures (rixtex)
`rix_texpool_init(base, pages, cacheable)` : base alignée 2 Kio, jusqu'à `RIX_TEX_MAX_PAGES` 8192
(16 Mio), n'importe où en DDR lisible par HP3 ; un seul pool par processus (le reinitialiser
abandonne les objets précédents sans les toucher). `rix_tex_alloc(&t, w, h, mag_linear)`
(puissances de 2, 8…256, premier emplacement libre, `RIXE_TEX_POOL` si plein, `RIXE_TEX_SIZE` si
hors limites) ; `rix_tex_free` ; `rix_texpool_free_pages()` / `rix_texpool_largest_free()`
(fragmentation). Uploads : `rix_tex_write_rgb565 / rgb888 / pal8(&t, idx, palette565, byteswap)`,
`rix_palette_rgb888_to_565()` (palette Quake II 768 octets), `rix_tex_write_rect_rgb565()` (sous-
rectangle, comme `glTexSubImage2D` : blocs de lightmap dynamiques, seules les lignes touchées sont
nettoyées du cache). `t.cfg` = valeur REG 12 ; `t.base`, `t.pages`. Pas de mipmaps en 1.1 (config
`min filter` à 0 ; un renderer choisit lui-même le niveau de détail qu'il charge).

Politique de remplacement : la bibliothèque n'en impose pas. Les faits qui la dictent : la TMU ne
garde qu'une texture, donc le « cache » est la DDR elle-même (une texture résidente dans le pool
coûte un bind de 16–233 µs à chaque usage, pas plus qu'une texture fraîchement écrite) ; le pool
de 16 Mio tient toutes les textures d'un niveau Quake II à 128×128 (< 4 Mio) ; un pool plein se
gère par `rix_tex_free` des textures du niveau précédent (le renderer connaît ses résidents, la
bibliothèque non). Le benchmark ROOM mesure le coût réel des deux stratégies (ZZQUAKE2_RIX_INTEGRATION.md).

## Sondes (SELFTEST PROBE)
`rix_run_probes(color, zbuf, tex)` : 0 depth (deux ordres + relance sans clear), 1 blending ONE,ONE,
2 alpha GREATER 0,5, 3/4/5 coût des switches 64×64 / 256×256 (GPU et MMIO séparés, état seul en
référence), **7 env TMU MODULATE** (texel blanc × couleur de sommet 0,5 → moitié attendue),
**8 blend ZERO,SRC_COLOR** (la passe lightmap de GL : blanc puis gris 0,5 → moitié).
**`rix_probe_ddr_reads(color, zbuf, tex)` (sondes 9–13, 1.1c)** : listes DDR dont les fragments
lisent la mémoire — 9 profondeur 2 triangles, 10 blend ZERO,SRC_COLOR 2 triangles, 11 profondeur
12 triangles, 12 profondeur 40 triangles, 13 profondeur + texture (bindée par la FIFO avant) 40
triangles — de la plus petite à la plus grande, arrêt au premier figeage (`E1C9`–`E1CD`,
`SD_PROBE_DDR_RES` : 1 WORKS, 2 mauvais pixel, 3 figé, 4 sautée faute de listes DDR). Règle 12.
`rix_probe_placement(color, tex_src, tex_dst, zbuf)` (sonde 6) : texture **écrite avec un texel
constant choisi à chaque run** (`SD_PROBE_PLACE_EXPECT`) et Z hors fenêtre ; le pixel rendu doit
être ce texel (à 1 unité par canal près) — la version D1 recopiait la même texture chrome à chaque
run et ne testait que ≠ 0, ce qu'un contenu périmé satisfait aussi. Résultats `SD_PROBE_*`,
verdicts repris par `rix_hw_caps()`. Les sondes écrivent dans SCRATCH et DL2 (slots 3 et 2 de
l'anneau des démos) et chargent leurs propres textures dans la TMU : à n'appeler que sans image en
vol ; elles invalident le suivi de texture de rixframe en sortant. Les sondes 7 et 8 sont nouvelles en
1.1 : leur valeur de registre MODULATE (`0x00A9C709`) est déduite du décodage bit à bit de la
valeur REPLACE validée (`0x00A9C700` = les défauts GL de TexEnv) ; **à confirmer sur l'Amiga**.

## Codes d'erreur
E110 ID, E111–E114 FIFO (E114 : paquet non pris en 100 ms), E120 fence timeout, E121 valeur, E122
liste trop grande, E123 descripteur, E124 anneau (fence de morceau jamais vue), E125 configuration
anneau / API, E126 pool de textures, E127 taille de texture, **E128 configuration `rix_config`
incomplète**, **E129 fence de bind jamais passée** (le GPU s'est arrêté sur l'`OP_TEXTURE`), E130/E131 DMA, E140 aucun flip, E150 timer, E160 mode, E1A0 GPU figé au lancement,
**E132 auto-test du transport PL330→FIFO échoué** (`SD_DMAFIFO_STATUS`), E1Bk étape k de l'échelle, E1Cx sonde x (`E1C9`–`E1CD` : sondes DDR 9–13), **E12A fence de morceau jamais
passée (mode SYNC)**, **E1F1/E1F3/E1F4 exception CPU Core1** (undefined / prefetch abort / data abort,
`SD_EXC_*` : PC, FAR, FSR, SPSR — les vecteurs sont installés par la démo, startup_return.S, pas par la
bibliothèque). Les démos les affichent en `0x5249xxxx`.

## Propriété des ressources
| ressource | propriétaire | qui écrit | qui lit |
|---|---|---|---|
| buffers couleur 640×481 (fenêtre, NC) | application | GPU (pixels, fences), Core1 (zéro des fences) | 68k (scanout/PAN), Core1 (fences) |
| Z-buffer 640×481×16 | application (adresse dans `rix_frame_desc.zbuf`) | GPU seul | GPU seul |
| anneau de morceaux (`rix_ring_cfg`) | application (zone), rixframe (contenu) | Core1 | FSC (DDR) |
| pool de textures (`rix_texpool_init`) | application (zone), rixtex (pages) | Core1 (`rix_tex_write_*`) | TMU (HP3) |
| `rix_config.stats/sd` | application | rixgpu | application, 68k |
| `rix_config.dma_prog` (64 o NC) | application | rixgpu | PL330 |
| registres RasterIX | rixframe pendant une image (REG 0/3 ombrés), application entre les images | — | — |
| TMU (texture chargée) | rixframe (suivi `g_bound_*`) | binds | — |

## Limites 1.1
640×481 fixe (`RIX_W/RIX_H_ALLOC`), une TMU, pas de mipmaps ni de stencil dans l'API (registres
accessibles par `rix_frame_reg`), l'échelle d'init utilise la disposition fixe de la fenêtre G4B
(`rix_window.h`) ; `ring_cacheable`, les sondes 7/8, `rix_tex_write_rect_rgb565`, le clipping et
le benchmark ROOM n'ont pas encore tourné sur l'Amiga (BILAN.md).
