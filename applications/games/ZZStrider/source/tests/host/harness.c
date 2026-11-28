/*
 * harness.c - host regression harness for the ZZStrider Core1 blob (tests only).
 *
 * It links the REAL blob sources (Cyclone, CZ80, the CPS-1 engine, ROM decoder, sound board,
 * video conversion, and the command / frame logic of zzs_main.c, included below) and replaces
 * only what cannot exist under qemu-arm user mode: the MMU, the caches and the PMU counter.
 * The absolute addresses of the memory plan are mapped with mmap, so nothing is re-pointed.
 * It then plays the launcher: INIT, chunked ROM transfer, COMMIT, START, buffer hand-over.
 *
 * usage: qemu-arm harness.elf <rom directory> <output directory> [frames]
 * ASCII only.
 */
#define zzs_core1_main zzs_core1_main_blob     /* the bare-metal entry is not used on the host */
#include "../../arm/zzs_main.c"
#undef zzs_core1_main

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char _bss_start, _bss_end, _zzs_stack_bottom, _zzs_stack_top;

/* ---- replaced hardware ------------------------------------------------------------------ */
static zzs_u32 g_fake_pmu;
zzs_u32 zzs_pmu_cycles(void) { g_fake_pmu += 20000u; return g_fake_pmu; }
void zzs_pmu_enable(void) { }
void zzs_pmu_events(zzs_u32 *v) { int i; for (i = 0; i < ZZS_PMU_NEV; i++) v[i] = g_fake_pmu / (zzs_u32)(i + 2); }
static zzs_u32 g_one_way_on, g_one_way_calls;
int g_zzs_l2_fix = 1;
zzs_u32 zzs_mmu_par(zzs_u32 a) { return (a & 0xFFFFF000u) | 0xD4u; }
zzs_u32 zzs_l2_enabled(void) { return 1u; }
zzs_u32 zzs_actlr_one_way(int on) { g_one_way_on = (zzs_u32)on; g_one_way_calls++; return on ? 0x147u : 0x47u; }
void mmu_init(void) { }
void zz_install_vectors(void) { }
static zzs_u32 g_wbwa_calls;
void mmu_set_wbwa(zzs_u32 addr, zzs_u32 len) { (void)addr; (void)len; g_wbwa_calls++; }
zzs_u32 mmu_get_desc(zzs_u32 addr) { (void)addr; return 0; }
void dcache_clean_range(void *addr, zzs_u32 len) { (void)addr; (void)len; }
void dcache_inval_range(void *addr, zzs_u32 len) { (void)addr; (void)len; }

/* Linux mmap2 through a raw syscall (qemu-arm user mode), newlib has no mmap. */
static long sys_mmap_fixed(unsigned long addr, unsigned long len)
{
    register long r0 __asm__("r0") = (long)addr;
    register long r1 __asm__("r1") = (long)len;
    register long r2 __asm__("r2") = 3;            /* PROT_READ | PROT_WRITE */
    register long r3 __asm__("r3") = 0x32;         /* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED */
    register long r4 __asm__("r4") = -1;
    register long r5 __asm__("r5") = 0;
    register long r7 __asm__("r7") = 192;          /* mmap2 */
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory");
    return r0;
}

#define VRAM_BASE 0x02000000u                      /* fake Picasso96 VRAM, below the blob */
#define VRAM_PITCH (ZZS_SCREEN_W * 4u)
#define VRAM_BUF   (VRAM_PITCH * ZZS_SCREEN_H)

extern unsigned char *CpsRamFF;                    /* 68000 work RAM, host-order words */
const zzs_u16 *zzs_video_index_frame(void);
const zzs_u32 *zzs_machine_palette(void);

static const char *g_names[ZZS_ROM_FILE_COUNT] = {
#define X(i, n, s) n,
    ZZS_ROM_LIST(X)
#undef X
};
static const zzs_u32 g_sizes[ZZS_ROM_FILE_COUNT] = {
#define X(i, n, s) s,
    ZZS_ROM_LIST(X)
#undef X
};

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void cmd(zzs_u32 c)
{
    ZZS_SH(ZZS_SH_COMMAND) = c;
    ZZS_SH(ZZS_SH_CMD_SEQ)++;
    process_command(c);
    ZZS_SH(ZZS_SH_ACK_SEQ) = ZZS_SH(ZZS_SH_CMD_SEQ);
}

static int send_chunk(zzs_u32 file, zzs_u32 off, const unsigned char *data, zzs_u32 len)
{
    memcpy((void *)ZZS_ARM_STAGE, data, len);
    ZZS_SH(ZZS_SH_ROM_FILE) = file;
    ZZS_SH(ZZS_SH_ROM_OFFSET) = off;
    ZZS_SH(ZZS_SH_ROM_LENGTH) = len;
    cmd(ZZS_CMD_ROM_CHUNK);
    return (int)ZZS_SH(ZZS_SH_ERROR);
}

static unsigned char *load(const char *dir, const char *name, zzs_u32 size)
{
    char path[512];
    unsigned char *p = malloc(size);
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f || !p || fread(p, 1, size, f) != size) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
    return p;
}

static void dump(const char *dir, const char *name, const void *p, zzs_u32 n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(2); }
    fwrite(p, 1, n, f);
    fclose(f);
}

static zzs_u16 ram16(zzs_u32 a) { return *(zzs_u16 *)(void *)(CpsRamFF + (a & 0xFFFFu)); }

int main(int argc, char **argv)
{
    const char *romdir = argc > 1 ? argv[1] : ".";
    const char *outdir = argc > 2 ? argv[2] : ".";
    zzs_u32 frames = argc > 3 ? (zzs_u32)atoi(argv[3]) : 130u;
    zzs_u32 rows = (argc > 4 && argv[4][0] == 's') ? ((argv[4][4] == '4') ? 224u : 240u) : ZZS_SCREEN_H; /* scan / scan448 */
    zzs_u32 f, i, last_ready = 0, panned = 0, buf = 0, pcm_rd = 0;
    static const zzs_u32 sizes[4] = { 0x10000u, 0x7FFEu, 0x2u, 0xFFFEu };
    char name[512];
    FILE *pcm;

    if (sys_mmap_fixed(ZZS_ARM_SHARED, 0x1000) != (long)ZZS_ARM_SHARED ||
        sys_mmap_fixed(ZZS_ARM_STAGE, ZZS_STAGE_CHUNK) != (long)ZZS_ARM_STAGE ||
        sys_mmap_fixed(ZZS_ARM_PCM, ZZS_PCM_SIZE) != (long)ZZS_ARM_PCM ||
        sys_mmap_fixed(ZZS_ARM_ROMSTORE, ZZS_ARM_ARENA_END - ZZS_ARM_ROMSTORE) != (long)ZZS_ARM_ROMSTORE ||
        sys_mmap_fixed(VRAM_BASE, 3u * VRAM_BUF) != (long)VRAM_BASE) {
        printf("mmap of the fixed plan failed\n");
        return 2;
    }

    shared_defaults();
    ZZS_SH(ZZS_SH_STATUS) = ZZS_ST_READY;

    /* ---- protocol error paths ---- */
    cmd(ZZS_CMD_INIT);
    CHECK(ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_READY && ZZS_SH(ZZS_SH_ERROR) == 0, "INIT");
    cmd(ZZS_CMD_START);
    CHECK(ZZS_SH(ZZS_SH_ERROR) == ZZS_ERR_NOT_READY && ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_ERROR, "START without ROM");
    cmd(ZZS_CMD_INIT);
    CHECK(send_chunk(0, 0, (const unsigned char *)"....", 4) == ZZS_ERR_ROM_STATE, "CHUNK before BEGIN");
    cmd(ZZS_CMD_INIT); cmd(ZZS_CMD_ROM_BEGIN);
    CHECK(send_chunk(0, 0x100, (const unsigned char *)"....", 4) == ZZS_ERR_ROM_ORDER, "out of order chunk");
    cmd(ZZS_CMD_INIT);
    CHECK(send_chunk(0, 0, (const unsigned char *)"....", 3) == ZZS_ERR_ROM_BOUNDS, "odd length");
    cmd(ZZS_CMD_INIT);
    CHECK(send_chunk(16, 0, (const unsigned char *)"....", 4) == ZZS_ERR_ROM_BOUNDS, "file index");
    cmd(ZZS_CMD_INIT);
    cmd(ZZS_CMD_ROM_COMMIT);
    CHECK(ZZS_SH(ZZS_SH_ERROR) == ZZS_ERR_ROM_INCOMPLETE, "COMMIT of an incomplete set");

    /* ---- real transfer, with uneven chunk sizes ---- */
    cmd(ZZS_CMD_INIT); cmd(ZZS_CMD_ROM_BEGIN);
    for (i = 0; i < ZZS_ROM_FILE_COUNT; i++) {
        unsigned char *d = load(romdir, g_names[i], g_sizes[i]);
        zzs_u32 off = 0, k = i;
        while (off < g_sizes[i]) {
            zzs_u32 n = sizes[k++ & 3u];
            if (n > g_sizes[i] - off) n = g_sizes[i] - off;
            if (send_chunk(i, off, d + off, n) != 0) {
                printf("chunk rejected: file %u off %u err %u aux %08x\n", (unsigned)i, (unsigned)off,
                       (unsigned)ZZS_SH(ZZS_SH_ERROR), (unsigned)ZZS_SH(ZZS_SH_ERROR_AUX));
                return 2;
            }
            off += n;
        }
        free(d);
    }
    CHECK(ZZS_SH(ZZS_SH_ROM_FILES_OK) == 0xFFFFu, "files ok mask %08x", (unsigned)ZZS_SH(ZZS_SH_ROM_FILES_OK));
    CHECK(ZZS_SH(ZZS_SH_ROM_BYTES) == ZZS_ROM_TOTAL_BYTES, "byte count");
    cmd(ZZS_CMD_ROM_COMMIT);
    CHECK(ZZS_SH(ZZS_SH_ERROR) == 0, "COMMIT err %u", (unsigned)ZZS_SH(ZZS_SH_ERROR));
    printf("commit: ssp=%08x pc=%08x chunks=%u\n", (unsigned)ZZS_SH(ZZS_SH_RESET_SSP),
           (unsigned)ZZS_SH(ZZS_SH_RESET_PC), (unsigned)ZZS_SH(ZZS_SH_ROM_CHUNKS));
    dump(outdir, "store_gfx.bin", g_zzs_gfx, 0x402000u);
    dump(outdir, "store_prg.bin", g_zzs_prg, 0x100000u);
    dump(outdir, "store_z80.bin", g_zzs_z80, 0x10000u);
    dump(outdir, "store_oki.bin", g_zzs_oki, 0x40000u);

    /* ---- run, playing the launcher's part ---- */
    ZZS_SH(ZZS_SH_P96_W) = ZZS_SCREEN_W;
    ZZS_SH(ZZS_SH_P96_H) = rows;
    ZZS_SH(ZZS_SH_P96_PITCH) = VRAM_PITCH;
    ZZS_SH(ZZS_SH_P96_BASE) = VRAM_BASE;
    ZZS_SH(ZZS_SH_RENDER_SEQ) = 1;
    ZZS_SH(ZZS_SH_P96_ENABLE) = 1;
    ZZS_SH(ZZS_SH_INPUT_P1) = 0x13;                  /* right + left + button 1 */
    ZZS_SH(ZZS_SH_INPUT_P2) = 0x20;                  /* button 2 */
    ZZS_SH(ZZS_SH_INPUT_SYS) = 0x11;                 /* coin 1 + start 1 */
    cmd(ZZS_CMD_START);
    if (ZZS_SH(ZZS_SH_STATUS) != ZZS_ST_RUNNING) {
        printf("START failed: status %x error %u aux %08x\n", (unsigned)ZZS_SH(ZZS_SH_STATUS),
               (unsigned)ZZS_SH(ZZS_SH_ERROR), (unsigned)ZZS_SH(ZZS_SH_ERROR_AUX));
        return 2;
    }
    ZZS_SH(ZZS_SH_PCM_ENABLE) = 1;                   /* the launcher does this once AHI is up */
    snprintf(name, sizeof(name), "%s/audio_s16le_22050_mono.raw", outdir);
    pcm = fopen(name, "wb");

    for (f = 0; f < frames; f++) {
        run_one_frame();
        if (ZZS_SH(ZZS_SH_STATUS) != ZZS_ST_RUNNING) {
            printf("stopped at frame %u: status %x error %u aux %08x\n", (unsigned)f,
                   (unsigned)ZZS_SH(ZZS_SH_STATUS), (unsigned)ZZS_SH(ZZS_SH_ERROR),
                   (unsigned)ZZS_SH(ZZS_SH_ERROR_AUX));
            g_fail++;
            break;
        }
        if (ZZS_SH(ZZS_SH_FRAME_READY) != last_ready) {      /* "PAN", then hand the next buffer */
            last_ready = ZZS_SH(ZZS_SH_FRAME_READY);
            panned++;
            if (f == 25u || f == 50u || f == 80u || f == 110u) {
                snprintf(name, sizeof(name), "native_%03u.idx", (unsigned)f);
                dump(outdir, name, zzs_video_index_frame(), ZZS_SRC_W * ZZS_SRC_H * 2u);
                snprintf(name, sizeof(name), "palette_%03u.bin", (unsigned)f);
                dump(outdir, name, zzs_machine_palette(), 0x1000u * 4u);
                snprintf(name, sizeof(name), "screen_%03u.argb", (unsigned)f);
                dump(outdir, name, (void *)(VRAM_BASE + buf * VRAM_BUF), VRAM_PITCH * rows);
            }
            buf = (buf + 1u) % 3u;
            ZZS_SH(ZZS_SH_P96_BASE) = VRAM_BASE + buf * VRAM_BUF;
            ZZS_SH(ZZS_SH_RENDER_SEQ)++;
        }
        {   /* AHI's part: consume what was produced */
            zzs_u32 wr = ZZS_SH(ZZS_SH_PCM_WRITE);
            while (pcm_rd != wr) {
                fputc(((unsigned char *)ZZS_ARM_PCM)[pcm_rd], pcm);
                pcm_rd = (pcm_rd + 1u) % ZZS_PCM_SIZE;
            }
            ZZS_SH(ZZS_SH_PCM_READ) = pcm_rd;
        }
    }
    fclose(pcm);
    cmd(ZZS_CMD_STOP);
    CHECK(ZZS_SH(ZZS_SH_STATUS) == ZZS_ST_STOPPING, "STOP");

    printf("frames emulated=%u rendered=%u presented=%u wbwa_calls=%u\n", (unsigned)ZZS_SH(ZZS_SH_FRAME),
           (unsigned)ZZS_SH(ZZS_SH_RENDERED), (unsigned)panned, (unsigned)g_wbwa_calls);
    CHECK(g_one_way_on == 0u && g_one_way_calls == 2u * ZZS_SH(ZZS_SH_RENDERED), "one-way allocation left on or unbalanced");
    printf("conversion method=%u one_way_calls=%u pump_calls=%u rate=%u..%u backlog=%u (%u..%u) fifo_drop=%u\n",
           (unsigned)ZZS_SH(ZZS_SH_CONV_METHOD), (unsigned)g_one_way_calls, (unsigned)ZZS_SH(ZZS_SH_PUMP_CALLS),
           (unsigned)ZZS_SH(ZZS_SH_SND_RATE_MIN), (unsigned)ZZS_SH(ZZS_SH_SND_RATE_MAX), (unsigned)ZZS_SH(ZZS_SH_SND_BACKLOG),
           (unsigned)ZZS_SH(ZZS_SH_SND_BACKLOG_MIN), (unsigned)ZZS_SH(ZZS_SH_SND_BACKLOG_MAX), (unsigned)ZZS_SH(ZZS_SH_AQ_DROP_OLD));
    printf("ram: in1=%04x in0=%04x dswa=%04x dswb=%04x dswc=%04x f=%u mid=%04x,%04x hi=%04x,%04x magic=%04x trap=%04x\n",
           ram16(0x8000), ram16(0x8002), ram16(0x8004), ram16(0x8006), ram16(0x8008), ram16(0x800A),
           ram16(0x800C), ram16(0x800E), ram16(0x8010), ram16(0x8012), ram16(0x8020), ram16(0x8100));
    for (i = 0; i < ZZS_SH_PCTRACE0; i++)
        printf("slot %3u = %08x%s", (unsigned)i, (unsigned)ZZS_SH(i), (i % 4u) == 3u ? "\n" : "   ");
    printf("\nharness failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
