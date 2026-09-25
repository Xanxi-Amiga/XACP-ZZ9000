/* Host tests for the Core1 preload service using sub-4-GiB fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zzdf_host_alloc.h"


/* ---- host shim of the ZZ9000 map ---------------------------------- */
static unsigned int   g_fake_shared[1024];
static unsigned char *g_staging;
static unsigned char *g_arena;
#define HOST_STAGING_SIZE (4u*1024u*1024u)
#define HOST_ARENA_SIZE   (8u*1024u*1024u)

#define ZZDF_TEST_BUILD 1
#define ZZDF_SHARED_ARM        ((unsigned long)(unsigned long long)g_fake_shared)
#define ZZDF_STAGING_ARM       ((unsigned long)(unsigned long long)g_staging)
#define ZZDF_STAGING_SIZE      ((unsigned long)HOST_STAGING_SIZE)
#define ZZDF_ASSET_ARENA_BASE  ((unsigned int)(unsigned long long)g_arena)
#define ZZDF_ASSET_ARENA_END   (ZZDF_ASSET_ARENA_BASE + HOST_ARENA_SIZE)

#include "../core1/zzdf_config.h"
#include "../core1/zzdf_preload.c"

static int g_pass = 0, g_fail = 0;
static void ok(int c, const char *w)
{
    if (c) { g_pass++; printf("  PASS  %s\n", w); }
    else   { g_fail++; printf("  FAIL  %s\n", w); }
}

#define STG  ((unsigned int)ZZDF_STAGING_ARM)
#define ARB  ((unsigned int)ZZDF_ASSET_ARENA_BASE)
#define ARE  ((unsigned int)ZZDF_ASSET_ARENA_END)

static void test_validate(void)
{
    printf("\nvalidation (the exact ZZQuake clause list)\n");
    ok(zzdf_preload_validate(STG, ARB, 65536) == 0,
       "valid src/dst/size is accepted");
    ok(zzdf_preload_validate(STG + 1000, ARB + 0x100000, 12345) == 0,
       "unaligned valid request is accepted");
    ok(zzdf_preload_validate(STG, ARB, 0) != 0,
       "zero size is refused");
    ok(zzdf_preload_validate(STG - 4, ARB, 64) != 0,
       "src below staging is refused");
    ok(zzdf_preload_validate(STG + HOST_STAGING_SIZE - 16, ARB, 64) != 0,
       "src overrunning staging is refused");
    ok(zzdf_preload_validate(STG, ARB - 4, 64) != 0,
       "dst below the arena is refused");
    ok(zzdf_preload_validate(STG, ARE, 64) != 0,
       "dst at the guard is refused");
    ok(zzdf_preload_validate(STG, ARE + 0x1000, 64) != 0,
       "dst above the guard is refused");
    ok(zzdf_preload_validate(STG, ARE - 16, 64) != 0,
       "dst overrunning into the guard is refused");
    ok(zzdf_preload_validate(STG, ARB, HOST_STAGING_SIZE + 1) != 0,
       "size larger than staging is refused");
    ok(zzdf_preload_validate(STG, 0xFFFFFFF0u, 64) != 0,
       "dst wrap-around is refused");
}

 
static void test_validate_limit(void)
{
    unsigned int limit = ARB + 0x00800000u;      /* heap at +8 MiB */
    printf("\nCore1 dynamic limit: nothing may land at or above SH_HEAP_BASE\n");
    ok(zzdf_preload_validate_limit(STG, ARB, 65536, limit) == 0,
       "chunk well below the limit is accepted");
    ok(zzdf_preload_validate_limit(STG, limit - 65536, 65536, limit) == 0,
       "chunk ending EXACTLY at heap_base is accepted");
    ok(zzdf_preload_validate_limit(STG, limit - 65536, 65537, limit) != 0,
       "chunk overrunning heap_base by 1 byte is refused");
    ok(zzdf_preload_validate_limit(STG, limit, 64, limit) != 0,
       "chunk starting AT heap_base is refused");
    ok(zzdf_preload_validate_limit(STG, limit + 0x1000, 64, limit) != 0,
       "chunk starting inside the heap is refused");
    ok(zzdf_preload_validate_limit(STG, ARB, 64, 0) != 0,
       "SH_HEAP_BASE = 0 (unset) refuses everything");
    ok(zzdf_preload_validate_limit(STG, ARB, 64, ARB - 0x100000) != 0,
       "SH_HEAP_BASE below the super-zone is refused");
    ok(zzdf_preload_validate_limit(STG, ARB, 64, ARE + 0x100000) != 0,
       "SH_HEAP_BASE above the guard is refused");
    ok(zzdf_preload_validate_limit(STG, ARB, 64, ARB + 0x40) != 0,
       "unaligned SH_HEAP_BASE is refused");
    ok(zzdf_preload_validate_limit(STG, limit - 4, 0xFFFFFFF0u, limit) != 0,
       "dynamic overflow (size wraps past limit) is refused");
    ok(zzdf_preload_validate_limit(STG, ARB, 64, ARE) == 0,
       "limit at the guard degenerates to the static rule");
}

static void test_copy(void)
{
    unsigned int i, good;
    printf("\ncopy exactness\n");

    for (i = 0; i < 70000; i++) g_staging[i] = (unsigned char)(i * 13 + 7);
    memset(g_arena, 0xEE, 70000 + 64);

    zzdf_preload_copy(STG, ARB, 65536);
    good = 1;
    for (i = 0; i < 65536; i++)
        if (g_arena[i] != (unsigned char)(i * 13 + 7)) { good = 0; break; }
    ok(good, "64 KiB chunk copied byte-exact");
    ok(g_arena[65536] == 0xEE, "copy does not overrun by one byte");

    memset(g_arena, 0xEE, 70000 + 64);
    zzdf_preload_copy(STG, ARB, 4099);     /* words + 3-byte tail */
    good = 1;
    for (i = 0; i < 4099; i++)
        if (g_arena[i] != (unsigned char)(i * 13 + 7)) { good = 0; break; }
    ok(good, "odd-sized chunk (word + 3-byte tail) copied byte-exact");
    ok(g_arena[4099] == 0xEE, "tail copy does not overrun");
}

/* Simulate the launcher side of the handshake against the real
   Core1 state machine. */
static void test_state_machine(void)
{
    volatile unsigned int *sh = g_fake_shared;
    unsigned int last_ack = 0;
    int r;
    unsigned int i, good;

    printf("\nstate machine: PCOP -> PRDY -> COPY 1 -> ACK 1 -> COPY 2 -> ACK 2 -> PSTR\n");

    memset(g_fake_shared, 0, sizeof(g_fake_shared));

    /* launcher, before ARM_RUN: heap base + service request */
    sh[SH_HEAP_BASE] = ARB + 0x00400000u;        /* heap at +4 MiB */
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_COPY;

    /* Core1 announces READY (this is what zzdf_preload_service does
       before entering the step loop) */
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_READY;
    ok(sh[SH_PRELOAD_STATE] == ZZDF_PRELOAD_READY, "Core1 reports READY");

    /* idle step: nothing requested yet */
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 0 && sh[SH_PRELOAD_ACK] == 0, "idle step does nothing");

    /* chunk 1 */
    for (i = 0; i < 65536; i++) g_staging[i] = (unsigned char)(i ^ 0x5A);
    sh[SH_PRELOAD_SRC]  = STG;
    sh[SH_PRELOAD_DST]  = ARB;
    sh[SH_PRELOAD_SIZE] = 65536;
    sh[SH_PRELOAD_SEQ]  = 1;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 0, "step returns 'keep looping' after chunk 1");
    ok(sh[SH_PRELOAD_ACK] == 1, "ACK == 1");
    ok(sh[SH_PRELOAD_COUNT] == 1 && sh[SH_PRELOAD_BYTES] == 65536,
       "count/bytes updated");
    good = 1;
    for (i = 0; i < 65536; i++)
        if (g_arena[i] != (unsigned char)(i ^ 0x5A)) { good = 0; break; }
    ok(good, "chunk 1 landed at dst");

    /* same SEQ again: must be ignored, not re-copied */
    memset(g_staging, 0, 16);
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 0 && sh[SH_PRELOAD_ACK] == 1 && g_arena[0] == (0 ^ 0x5A),
       "repeated SEQ is ignored");

    /* chunk 2, at a different destination, 300 bytes */
    for (i = 0; i < 300; i++) g_staging[i] = (unsigned char)(i + 1);
    sh[SH_PRELOAD_DST]  = ARB + 65536;
    sh[SH_PRELOAD_SIZE] = 300;
    sh[SH_PRELOAD_SEQ]  = 2;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 0 && sh[SH_PRELOAD_ACK] == 2, "chunk 2 acked with SEQ 2");
    good = 1;
    for (i = 0; i < 300; i++)
        if (g_arena[65536 + i] != (unsigned char)(i + 1)) { good = 0; break; }
    ok(good, "chunk 2 landed at dst");
    ok(sh[SH_PRELOAD_COUNT] == 2 && sh[SH_PRELOAD_BYTES] == 65536 + 300,
       "count=2, bytes summed");

    /* launcher done */
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_START;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 1, "START makes the step return 'leave the loop'");
    ok(sh[SH_ERROR] == 0, "no error was raised");
}

static void test_refusal(void)
{
    volatile unsigned int *sh = g_fake_shared;
    unsigned int last_ack = 0;
    int r;

    printf("\nrefused request -> ERROR state, parked, no retry\n");
    memset(g_fake_shared, 0, sizeof(g_fake_shared));
    sh[SH_HEAP_BASE] = ARB + 0x00400000u;
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_READY;

    sh[SH_PRELOAD_SRC]  = STG;
    sh[SH_PRELOAD_DST]  = ARE - 8;        /* would spill into the guard */
    sh[SH_PRELOAD_SIZE] = 64;
    sh[SH_PRELOAD_SEQ]  = 7;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == -1, "step returns 'park'");
    ok(sh[SH_PRELOAD_STATE] == ZZDF_PRELOAD_ERROR, "STATE == PERR");
    ok(sh[SH_PRELOAD_ERR] == ARE - 8, "ERR holds the bad destination");
    ok((sh[SH_ERROR] & 0xFFFFFF00u) == 0xE0002000u, "SH_ERROR is a preload code");
    ok(sh[SH_PRELOAD_ACK] != 7, "the bad request was NOT acked");

    /* even a later valid request must not be served: parked */
    sh[SH_PRELOAD_DST]  = ARB;
    sh[SH_PRELOAD_SEQ]  = 8;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == -1 && sh[SH_PRELOAD_ACK] != 8, "after ERROR nothing is served");
}

 
static void test_refusal_heap(void)
{
    volatile unsigned int *sh = g_fake_shared;
    unsigned int last_ack = 0;
    int r;
    printf("\nstate machine refuses a chunk that would touch the live heap\n");
    memset(g_fake_shared, 0, sizeof(g_fake_shared));
    sh[SH_HEAP_BASE] = ARB + 0x00400000u;
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_READY;
    sh[SH_PRELOAD_SRC]  = STG;
    sh[SH_PRELOAD_DST]  = ARB + 0x00400000u - 100;   /* 100 bytes under heap */
    sh[SH_PRELOAD_SIZE] = 101;                        /* one too many */
    sh[SH_PRELOAD_SEQ]  = 3;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == -1 && sh[SH_PRELOAD_STATE] == ZZDF_PRELOAD_ERROR,
       "101 bytes ending 1 past heap_base -> PERR");
    ok((sh[SH_ERROR] & 0xFF) == 14, "reason code 14 = size past dynamic limit");

    memset(g_fake_shared, 0, sizeof(g_fake_shared));
    sh[SH_HEAP_BASE] = ARB + 0x00400000u;
    sh[SH_PRELOAD_STATE] = ZZDF_PRELOAD_READY;
    sh[SH_PRELOAD_SRC]  = STG;
    sh[SH_PRELOAD_DST]  = ARB + 0x00400000u - 100;
    sh[SH_PRELOAD_SIZE] = 100;                        /* exactly to the edge */
    sh[SH_PRELOAD_SEQ]  = 4;
    r = zzdf_preload_step(sh, &last_ack);
    ok(r == 0 && sh[SH_PRELOAD_ACK] == 4, "100 bytes ending exactly at heap_base -> ACK");
}

int main(void)
{
    printf("ZZDarkForces preload service host tests\n");
    printf("=======================================\n");
    g_staging = (unsigned char *)alloc32(HOST_STAGING_SIZE);
    /* the real arena base is 1 MiB aligned (0x30000000); the dynamic
       limit check requires that, so align the fake one the same way */
    {
        unsigned char *raw = (unsigned char *)alloc32(HOST_ARENA_SIZE + 64 + (1u << 20));
        unsigned long a = (unsigned long)(unsigned long long)raw;
        a = (a + (1u << 20) - 1) & ~((unsigned long)(1u << 20) - 1);
        g_arena = (unsigned char *)(unsigned long long)a;
    }
    memset(g_staging, 0, HOST_STAGING_SIZE);
    memset(g_arena, 0, HOST_ARENA_SIZE + 64);

    test_validate();
    test_validate_limit();
    test_copy();
    test_state_machine();
    test_refusal();
    test_refusal_heap();

    printf("\n=======================================\n");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
