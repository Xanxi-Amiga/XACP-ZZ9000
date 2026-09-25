/* Host tests for the shared MIDI ring. */
 






















#include <stdio.h>
#include <string.h>

#define RING_LEN 1024

static unsigned long ring[RING_LEN];
static unsigned int  head, tail, dropped, sent;
static int           midiOn = 1;

static int g_pass = 0, g_fail = 0;
static void ok(int c, const char *w)
{
    if (c) { g_pass++; }
    else   { g_fail++; printf("  FAIL  %s\n", w); }
}

/* Mirrors zzdf_midi_ring_put() in platform/zzdf_midi.cpp. */
static void ringPut(unsigned char st, unsigned char d1, unsigned char d2)
{
    unsigned int next;
    if (!midiOn) return;
    next = (head + 1u) % RING_LEN;
    if (next == tail) { dropped++; return; }
    ring[head] = ((unsigned long)st << 24) | ((unsigned long)d1 << 16) |
                 ((unsigned long)d2 << 8);
    head = next;
    sent++;
}

static void reset(void)
{
    memset(ring, 0, sizeof(ring));
    head = tail = dropped = sent = 0;
    midiOn = 1;
}

/* ---- the CAMD word ----------------------------------------------- */
static void test_word(void)
{
    reset();
    /* PutMidi takes status<<24 | d1<<16 | d2<<8. The low byte is the
       CAMD port and must stay clear. */
    ringPut(0x93, 60, 100);
    ok((ring[0] >> 24) == 0x93,          "status in the top byte");
    ok(((ring[0] >> 16) & 0xFF) == 60,   "data1 next");
    ok(((ring[0] >> 8) & 0xFF) == 100,   "data2 next");
    ok((ring[0] & 0xFF) == 0,            "low byte clear - it is the CAMD port");

    /* A program change is two bytes; upstream passes len 2 and the
       third byte is simply left at zero. */
    reset();
    ringPut(0xC5, 48, 0);
    ok(ring[0] == 0xC5300000ul, "two-byte program change packs exactly");

    /* Note Off with velocity 0 must still be a full three-byte word. */
    reset();
    ringPut(0x80, 60, 0);
    ok(ring[0] == 0x803C0000ul, "note off packs exactly");
}

/* ---- ordering and wrap ------------------------------------------- */
static void test_order(void)
{
    unsigned int i;
    reset();
    for (i = 0; i < 100u; i++) ringPut(0x90, (unsigned char)(i & 0x7F), 64);
    ok(head == 100u, "head advanced once per message");
    for (i = 0; i < 100u; i++)
        if (((ring[i] >> 16) & 0xFF) != (i & 0x7F)) { ok(0, "order preserved"); return; }
    ok(1, "order preserved");

    /* The drain wraps with the same modulus. */
    reset();
    head = tail = RING_LEN - 2u;
    ringPut(0x90, 1, 1);
    ringPut(0x90, 2, 2);
    ok(head == 0u, "head wraps to 0");
    ok(((ring[RING_LEN-2] >> 16) & 0xFF) == 1, "last slot holds the first");
    ok(((ring[RING_LEN-1] >> 16) & 0xFF) == 2, "and the seam holds the second");
}

/* ---- full ring drops, never blocks, never passes head over tail --- */
static void test_full(void)
{
    unsigned int i;
    reset();
    for (i = 0; i < RING_LEN + 50u; i++) ringPut(0xB0, 1, (unsigned char)(i & 0x7F));
    ok(dropped == 51u, "a full ring drops and counts");
    ok(head != tail,   "head never catches tail - full would read as empty");
    ok(sent == RING_LEN - 1u, "one slot stays free by construction");

    /* Draining frees space again. */
    tail = (tail + 10u) % RING_LEN;
    dropped = 0;
    for (i = 0; i < 10u; i++) ringPut(0xB0, 1, 0);
    ok(dropped == 0u, "space freed by the drain is reusable");
}

/* ---- the link-down gate ------------------------------------------ */
static void test_gate(void)
{
    reset();
    midiOn = 0;
    ringPut(0x90, 60, 100);
    ok(head == 0u && sent == 0u, "no camd link, nothing queued");
    ok(dropped == 0u, "and it is not counted as a loss");
}

int main(void)
{
    printf("MIDI ring tests\n");
    test_word();
    test_order();
    test_full();
    test_gate();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
