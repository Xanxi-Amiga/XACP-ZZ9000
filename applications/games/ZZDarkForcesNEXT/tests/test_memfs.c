/* Host tests for the Core1 in-memory filesystem. */
 














#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zzdf_host_alloc.h"


/* ---- host shim: fake the ZZ9000 address map -----------------------
 * zzdf_config.h hardcodes ARM addresses. On the host we cannot use
 * them as real pointers, so we redirect the two things MemFS touches:
 * the shared page and the asset-arena bounds check.
 */
static unsigned int g_fake_shared[1024];
static unsigned char *g_arena;
#define ZZDF_HOST_ARENA_SIZE (4u*1024u*1024u)

/* arena base is a real host allocation; the bounds check in
   zzdf_fs_mount uses these two macros, so point them at it */
#define ZZDF_TEST_BUILD 1
#define ZZDF_SHARED_ARM        ((unsigned long)(unsigned long long)g_fake_shared)
#define ZZDF_ASSET_ARENA_BASE  ((unsigned int)(unsigned long long)g_arena)
#define ZZDF_ASSET_ARENA_END   (ZZDF_ASSET_ARENA_BASE + ZZDF_HOST_ARENA_SIZE)

#include "../core1/zzdf_config.h"

/* MemFS traces through the blob's log ring, which does not exist on the
   host. Send it to stderr so a trace stays visible without polluting
   the "62/62" line the harness reads from stdout. */
void zzdf_log_puts(const char *s) { fputs(s, stderr); }

/* the module under test, compiled verbatim */
#include "../core1/zzdf_fs_mem.c"

/* ---- tiny test framework ----------------------------------------- */
static int g_pass = 0, g_fail = 0;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", what); }
    else      { g_fail++; printf("  FAIL  %s\n", what); }
}

/* ---- fixtures ----------------------------------------------------- */
static unsigned char *g_manifest_mem;

/* a deterministic byte pattern so partial reads can be checked */
static unsigned char pat(unsigned int i) { return (unsigned char)(i * 7 + 13); }

static void build_fixture(void)
{
    zzdf_manifest_hdr *h;
    zzdf_manifest_entry *e;
    unsigned char *dark, *sprites;
    unsigned int i;

    g_arena = (unsigned char *)alloc32(ZZDF_HOST_ARENA_SIZE);
    memset(g_arena, 0, ZZDF_HOST_ARENA_SIZE);

    /* two synthetic archives inside the arena */
    dark    = g_arena + 0x1000;
    sprites = g_arena + 0x9000;
    for (i = 0; i < 4096; i++)  dark[i]    = pat(i);
    for (i = 0; i < 1000; i++)  sprites[i] = (unsigned char)(0xA0 + (i & 15));

    /* GOB-like header on the first one so the parser test has input */
    memcpy(dark, "GOB\x0A", 4);

    g_manifest_mem = (unsigned char *)alloc32(4096);
    memset(g_manifest_mem, 0, 4096);
    h = (zzdf_manifest_hdr *)g_manifest_mem;
    h->magic = ZZDF_MANIFEST_MAGIC;
    h->count = 4;
    h->total = 4096 + 1000;
    h->resv  = 0;

    e = (zzdf_manifest_entry *)(h + 1);
    strcpy(e[0].name, "DARK.GOB");
    e[0].base = (unsigned int)(unsigned long long)dark;
    e[0].size = 4096;
    strcpy(e[1].name, "SPRITES.GOB");
    e[1].base = (unsigned int)(unsigned long long)sprites;
    e[1].size = 1000;
     

    strcpy(e[2].name, "quicksave.tfe");
    e[2].base  = (unsigned int)(unsigned long long)(g_arena + 0x20000);
    e[2].size  = 64;
    e[2].mtime = 1000;                 /* older */
    strcpy(e[3].name, "quicksave2.tfe");
    e[3].base  = (unsigned int)(unsigned long long)(g_arena + 0x21000);
    e[3].size  = 64;
    e[3].mtime = 2000;                 /* newer */
    memset(g_arena + 0x20000, 0xE1, 64);
    memset(g_arena + 0x21000, 0xE2, 64);
    h->count = 4;

    g_fake_shared[SH_MANIFEST_ADDR] =
        (unsigned int)(unsigned long long)g_manifest_mem;
}

/* ---- tests -------------------------------------------------------- */
static void test_mount(void)
{
    unsigned int n;
    printf("\nmount / manifest validation\n");

    n = zzdf_fs_mount(g_manifest_mem);
    ok(n == 4, "valid manifest mounts, count = 4");
    ok(zzdf_fs_count() == 4, "zzdf_fs_count agrees");
    ok(zzdf_fs_entry(0) != 0 &&
       strcmp(zzdf_fs_entry(0)->name, "DARK.GOB") == 0,
       "entry 0 is DARK.GOB");
    ok(zzdf_fs_entry(4) == 0, "entry past the end returns NULL");

    ok(zzdf_fs_mount(0) == 0, "NULL manifest is rejected");

    {   /* wrong magic */
        zzdf_manifest_hdr bad;
        memset(&bad, 0, sizeof(bad));
        bad.magic = 0xDEADBEEF; bad.count = 1;
        ok(zzdf_fs_mount(&bad) == 0, "bad magic is rejected");
    }
    {   /* count out of range */
        unsigned char buf[4096];
        zzdf_manifest_hdr *h = (zzdf_manifest_hdr *)buf;
        memset(buf, 0, sizeof(buf));
        h->magic = ZZDF_MANIFEST_MAGIC;
        h->count = ZZDF_MANIFEST_MAX + 1;
        ok(zzdf_fs_mount(buf) == 0, "absurd entry count is rejected");
        h->count = 0;
        ok(zzdf_fs_mount(buf) == 0, "zero entry count is rejected");
    }
    {   /* entry pointing outside the asset arena must not mount:
           on hardware that would be a data abort at first read */
        unsigned char buf[4096];
        zzdf_manifest_hdr *h = (zzdf_manifest_hdr *)buf;
        zzdf_manifest_entry *e;
        memset(buf, 0, sizeof(buf));
        h->magic = ZZDF_MANIFEST_MAGIC; h->count = 1;
        e = (zzdf_manifest_entry *)(h + 1);
        strcpy(e->name, "STRAY.GOB");
        e->base = ZZDF_ASSET_ARENA_BASE - 0x10000;
        e->size = 16;
        ok(zzdf_fs_mount(buf) == 0, "entry below the arena is rejected");
        e->base = ZZDF_ASSET_ARENA_END - 8;
        e->size = 4096;
        ok(zzdf_fs_mount(buf) == 0, "entry overrunning the arena is rejected");
    }
    {   /* empty name / zero size */
        unsigned char buf[4096];
        zzdf_manifest_hdr *h = (zzdf_manifest_hdr *)buf;
        zzdf_manifest_entry *e;
        memset(buf, 0, sizeof(buf));
        h->magic = ZZDF_MANIFEST_MAGIC; h->count = 1;
        e = (zzdf_manifest_entry *)(h + 1);
        e->base = ZZDF_ASSET_ARENA_BASE; e->size = 16;
        ok(zzdf_fs_mount(buf) == 0, "empty name is rejected");
        strcpy(e->name, "X.GOB"); e->size = 0;
        ok(zzdf_fs_mount(buf) == 0, "zero size is rejected");
    }

    zzdf_fs_mount(g_manifest_mem);   /* restore the good one */
}

 
static void test_mount_limit(void)
{
    unsigned char buf[4096];
    zzdf_manifest_hdr *h = (zzdf_manifest_hdr *)buf;
    zzdf_manifest_entry *e;
    unsigned int limit = ZZDF_ASSET_ARENA_BASE + 0x9000;   /* heap "starts" here */

    printf("\nmount with dynamic asset limit (SH_HEAP_BASE)\n");

    memset(buf, 0, sizeof(buf));
    h->magic = ZZDF_MANIFEST_MAGIC; h->count = 1;
    e = (zzdf_manifest_entry *)(h + 1);
    strcpy(e->name, "EDGE.GOB");

    e->base = limit - 4096; e->size = 4096;
    ok(zzdf_fs_mount_limit(buf, limit) == 1, "entry ending EXACTLY at heap_base mounts");

    e->base = limit - 4096; e->size = 4097;
    ok(zzdf_fs_mount_limit(buf, limit) == 0, "entry overrunning heap_base by 1 is refused");

    e->base = limit; e->size = 16;
    ok(zzdf_fs_mount_limit(buf, limit) == 0, "entry starting at heap_base is refused");

    e->base = limit + 0x100; e->size = 16;
    ok(zzdf_fs_mount_limit(buf, limit) == 0, "entry starting inside the heap is refused");

    e->base = ZZDF_ASSET_ARENA_BASE; e->size = 64;
    ok(zzdf_fs_mount_limit(buf, ZZDF_ASSET_ARENA_BASE - 4) == 0, "limit below the arena is refused");
    ok(zzdf_fs_mount_limit(buf, ZZDF_ASSET_ARENA_END + 4) == 0, "limit above the guard is refused");
    ok(zzdf_fs_mount_limit(buf, ZZDF_ASSET_ARENA_END) == 1, "limit at the guard = static rule");

    zzdf_fs_mount(g_manifest_mem);   /* restore the good one */
}

static void test_open_case(void)
{
    int fd;
    printf("\nopen / case-insensitive names\n");

    fd = zzdf_fs_open("DARK.GOB", 0, 0);
    ok(fd >= 0, "open DARK.GOB");
    zzdf_fs_close(fd);

    fd = zzdf_fs_open("dark.gob", 0, 0);
    ok(fd >= 0, "open dark.gob (lower case)");
    zzdf_fs_close(fd);

    fd = zzdf_fs_open("Dark.Gob", 0, 0);
    ok(fd >= 0, "open Dark.Gob (mixed case)");
    zzdf_fs_close(fd);

    /* TFE builds paths like "<root>/DARK.GOB" */
    fd = zzdf_fs_open("/some/where/DARK.GOB", 0, 0);
    ok(fd >= 0, "open through a path prefix");
    zzdf_fs_close(fd);

    fd = zzdf_fs_open("NOSUCH.GOB", 0, 0);
    ok(fd < 0, "opening a missing file fails");

    ok(zzdf_fs_exists("SPRITES.GOB") == 1, "exists SPRITES.GOB");
    ok(zzdf_fs_exists("sprites.gob") == 1, "exists is case-insensitive");
    ok(zzdf_fs_exists("NOSUCH.GOB") == 0, "exists says no for a missing file");
}

static void test_read(void)
{
    int fd, n, i;
    unsigned char buf[8192];
    int good;
    printf("\nread: exact, partial, overrun\n");

    fd = zzdf_fs_open("DARK.GOB", 0, 0);
    ok(zzdf_fs_size(fd) == 4096, "size is 4096");

    memset(buf, 0, sizeof(buf));
    n = zzdf_fs_read(fd, buf, 4096);
    ok(n == 4096, "full read returns 4096");
    good = 1;
    for (i = 4; i < 4096; i++) if (buf[i] != pat((unsigned int)i)) good = 0;
    ok(good, "full read content matches the fixture");
    ok(memcmp(buf, "GOB\x0A", 4) == 0, "header bytes are intact");

    ok(zzdf_fs_tell(fd) == 4096, "tell is at the end");
    ok(zzdf_fs_eof(fd) == 1, "eof is set");

    /* read past the end must return 0, never garbage */
    n = zzdf_fs_read(fd, buf, 16);
    ok(n == 0, "reading past the end returns 0");
    zzdf_fs_close(fd);

    /* partial reads must be contiguous */
    fd = zzdf_fs_open("DARK.GOB", 0, 0);
    memset(buf, 0, sizeof(buf));
    n = zzdf_fs_read(fd, buf, 100);
    ok(n == 100, "first partial read = 100");
    ok(zzdf_fs_tell(fd) == 100, "tell advanced to 100");
    n = zzdf_fs_read(fd, buf + 100, 250);
    ok(n == 250, "second partial read = 250");
    good = 1;
    for (i = 4; i < 350; i++) if (buf[i] != pat((unsigned int)i)) good = 0;
    ok(good, "partial reads are contiguous");

    /* an oversized read must clamp to the file, not overrun the arena */
    zzdf_fs_lseek(fd, 4000, 0);
    n = zzdf_fs_read(fd, buf, 8192);
    ok(n == 96, "oversized read clamps to the remaining 96 bytes");
    zzdf_fs_close(fd);
}

static void test_seek(void)
{
    int fd, r;
    unsigned char b[4];
    printf("\nseek SET / CUR / END, tell\n");

    fd = zzdf_fs_open("DARK.GOB", 0, 0);

    r = zzdf_fs_lseek(fd, 1000, 0);
    ok(r == 1000, "SEEK_SET 1000");
    ok(zzdf_fs_tell(fd) == 1000, "tell = 1000");
    zzdf_fs_read(fd, b, 1);
    ok(b[0] == pat(1000), "byte at 1000 is correct");

    r = zzdf_fs_lseek(fd, 99, 1);
    ok(r == 1100, "SEEK_CUR +99 from 1001");
    zzdf_fs_read(fd, b, 1);
    ok(b[0] == pat(1100), "byte at 1100 is correct");

    r = zzdf_fs_lseek(fd, 0, 2);
    ok(r == 4096, "SEEK_END 0 = size");
    r = zzdf_fs_lseek(fd, -16, 2);
    ok(r == 4080, "SEEK_END -16");
    zzdf_fs_read(fd, b, 1);
    ok(b[0] == pat(4080), "byte at 4080 is correct");

    ok(zzdf_fs_lseek(fd, -1, 0) < 0, "negative SEEK_SET is refused");
    ok(zzdf_fs_lseek(fd, 99999, 0) < 0, "seek past the end is refused");
    ok(zzdf_fs_lseek(fd, 0, 77) < 0, "unknown whence is refused");

    zzdf_fs_close(fd);
}

static void test_badfd(void)
{
    unsigned char b[4];
    printf("\nbad descriptors\n");
    ok(zzdf_fs_read(9999, b, 4) < 0, "read on a bogus fd fails");
    ok(zzdf_fs_lseek(9999, 0, 0) < 0, "seek on a bogus fd fails");
    ok(zzdf_fs_tell(9999) < 0, "tell on a bogus fd fails");
    ok(zzdf_fs_close(9999) < 0, "close on a bogus fd fails");
    ok(zzdf_fs_is_ours(3) == 0, "fd 3 is not ours (console range)");
}

static void test_handles(void)
{
    int fd[ZZDF_MAX_OPEN + 2];
    int i, opened = 0;
    printf("\nhandle exhaustion\n");

    for (i = 0; i < ZZDF_MAX_OPEN + 2; i++) {
        fd[i] = zzdf_fs_open("DARK.GOB", 0, 0);
        if (fd[i] >= 0) opened++;
    }
    ok(opened == ZZDF_MAX_OPEN, "exactly ZZDF_MAX_OPEN handles are handed out");
    for (i = 0; i < ZZDF_MAX_OPEN + 2; i++)
        if (fd[i] >= 0) zzdf_fs_close(fd[i]);
    fd[0] = zzdf_fs_open("DARK.GOB", 0, 0);
    ok(fd[0] >= 0, "handles are reusable after close");
    zzdf_fs_close(fd[0]);
}

/* ---- GOB container sanity ----------------------------------------
 * A real DARK.GOB starts with "GOB\n" then a u32 offset to the file
 * index. We do not reimplement the parser here - TFE does that - but
 * we prove MemFS delivers the header and can seek to the index the
 * way TFE's archive code will.
 */
static void test_gob_access(void)
{
    int fd;
    unsigned char hdr[4];
    printf("\nGOB container access through MemFS\n");

    fd = zzdf_fs_open("DARK.GOB", 0, 0);
    ok(fd >= 0, "GOB opens");
    ok(zzdf_fs_read(fd, hdr, 4) == 4, "4-byte signature read");
    ok(hdr[0]=='G' && hdr[1]=='O' && hdr[2]=='B' && hdr[3]==0x0A,
       "signature is GOB\\n");
    ok(zzdf_fs_lseek(fd, 0, 2) == 4096, "can seek to the container end");
    ok(zzdf_fs_lseek(fd, 4, 0) == 4, "can seek back to the index pointer");
    zzdf_fs_close(fd);

    printf("  NOTE  a real DARK.GOB was not available in this build\n");
    printf("        environment; this is the synthetic fixture. The\n");
    printf("        real container is parsed by TFE_Archive on the ARM.\n");
}

 














static void test_seed_from_manifest(void)
{
    int fd;
    unsigned char buf[8];
    printf("\nN12 write-open seeds from the staged file\n");

    /* DARK.GOB is in the fixture manifest; opening it for WRITE without
       truncate must start from the staged bytes, not from nothing. */
    fd = zzdf_fs_open("DARK.GOB", 1, 0);
    ok(fd >= 0, "write-open of a staged name succeeds");
    ok(zzdf_fs_size(fd) == 4096, "size is the staged size, not 0");
    ok(zzdf_fs_read(fd, buf, 4) == 4, "the staged bytes read back");
    ok(buf[0]=='G' && buf[1]=='O' && buf[2]=='B' && buf[3]==0x0A,
       "and they are the staged content");
    zzdf_fs_close(fd);

    /* trunc is the engine explicitly saying "discard": that must still
       give an empty file, or nothing could ever be overwritten. */
    fd = zzdf_fs_open("TRUNCME.CFG", 1, 1);
    ok(fd >= 0, "write-open with trunc succeeds");
    ok(zzdf_fs_size(fd) == 0, "trunc still gives an empty file");
    zzdf_fs_close(fd);
}

static void test_writeback_table(void)
{
    int fd;
    const char *name = 0;
    const void *data = 0;
    unsigned int size = 0, n0, n1;
    printf("\nN12 writeback table\n");

    n0 = zzdf_fs_dirty_count();

    fd = zzdf_fs_open("DARKPILO.CFG", 1, 1);
    ok(fd >= 0, "save file opens for write");
    ok(zzdf_fs_write(fd, "AGENT", 5) == 5, "5 bytes written");
    zzdf_fs_close(fd);

    n1 = zzdf_fs_dirty_count();
    ok(n1 == n0 + 1, "the written file appears in the table exactly once");

    /* find it and check what the launcher would save */
    {
        unsigned int i; int found = 0;
        for (i = 0; i < n1; i++) {
            if (!zzdf_fs_dirty_entry(i, &name, &data, &size)) continue;
            if (name && name[0]=='D' && name[1]=='A' && name[2]=='R' &&
                name[3]=='K' && name[4]=='P') { found = 1; break; }
        }
        ok(found, "DARKPILO.CFG is in the table");
        ok(size == 5, "with the length actually written");
        ok(data && ((const char*)data)[0] == 'A', "and pointing at its bytes");
    }

    /* a file only ever READ must never be published */
    fd = zzdf_fs_open("SOUNDS.GOB", 0, 0);
    zzdf_fs_close(fd);
    ok(zzdf_fs_dirty_count() == n1, "a read-only file is not published");

    ok(zzdf_fs_dirty_entry(n1 + 16, &name, &data, &size) == 0,
       "an out-of-range index is refused");
}

 












static void test_write_capacity(void)
{
    int fd;
    unsigned int n_before, n_after, i;
    const char *name = 0; const void *data = 0; unsigned int size = 0;
    static unsigned char big[64 * 1024];
    unsigned int written = 0;
    int refused = 0;
    printf("\nr21c RAMFS capacity: refuse, never truncate\n");

    for (i = 0; i < sizeof(big); i++) big[i] = (unsigned char)(i & 0xFF);

    n_before = zzdf_fs_dirty_count();

    fd = zzdf_fs_open("HUGE.TFE", 1, 1);
    ok(fd >= 0, "oversized save opens for write");

     




    for (i = 0; i < (ZZDF_RAMF_CHUNK / sizeof(big)) + 8u; i++) {
        int r = zzdf_fs_write(fd, big, (unsigned int)sizeof(big));
        if (r < 0) { refused = 1; break; }
        if (r != (int)sizeof(big)) {
            ok(0, "a write was silently truncated (this is the old bug)");
            break;
        }
        written += (unsigned int)r;
    }
    ok(refused, "the write past capacity is REFUSED (-1), not shortened");
    ok(written == ZZDF_RAMF_CHUNK,
       "exactly the chunk was accepted before the refusal");

    zzdf_fs_close(fd);

    n_after = zzdf_fs_dirty_count();
    ok(n_after == n_before,
       "an overflowed file is WITHHELD from the writeback table");

    /* and it must not be reachable through the table by name either */
    {
        int found = 0;
        for (i = 0; i < n_after; i++) {
            if (!zzdf_fs_dirty_entry(i, &name, &data, &size)) continue;
            if (name && name[0] == 'H' && name[1] == 'U' && name[2] == 'G')
                found = 1;
        }
        ok(!found, "HUGE.TFE is not offered to the launcher");
    }

    /* MODE_NEWFILE starts over, so the next save of the same name gets
       a clean verdict. Without this reset one oversized save would
       poison that filename for the rest of the session. */
    fd = zzdf_fs_open("HUGE.TFE", 1, 1);
    ok(fd >= 0, "the same name reopens with truncate");
    ok(zzdf_fs_write(fd, "SMALL", 5) == 5, "a small write now succeeds");
    zzdf_fs_close(fd);
    ok(zzdf_fs_dirty_count() == n_before + 1,
       "and the file is published again");
}


 




static void test_savestates(void)
{
    int fd;
    static char payload[4096];

    printf("\nr31 savestates: read order, commit, ordering\n");

     
    fd = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 0, 0);
    ok(fd >= 0, "staged quicksave opens");
    if (fd >= 0) {
        unsigned char b = 0;
        zzdf_fs_read(fd, &b, 1);
        ok(b == 0xE1, "and it is the STAGED content");
        zzdf_fs_close(fd);
    }
    ok(zzdf_fs_mtime("quicksave.tfe")  == 1000, "staged mtime comes from the manifest");
    ok(zzdf_fs_mtime("quicksave2.tfe") == 2000, "the newer staged save sorts after");
    ok(zzdf_fs_mtime("quicksave2.tfe") > zzdf_fs_mtime("quicksave.tfe"),
       "order between two PREVIOUS-session saves survives the relaunch");

     
    fd = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 1, 1);
    ok(fd >= 0, "quicksave opens for writing");
    zzdf_fs_write(fd, payload, 64);
    {
        int r = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 0, 0);
        unsigned char b = 0;
        ok(r >= 0, "a reader during the write still gets a file");
        if (r >= 0) { zzdf_fs_read(r, &b, 1); zzdf_fs_close(r); }
        ok(b == 0xE1, "and it is STILL the staged one, not the half-written one");
    }
    zzdf_fs_close(fd);

     
    fd = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 0, 0);
    ok(fd >= 0, "quicksave reopens after the write");
    if (fd >= 0) {
        unsigned char b = 0xFF;
        zzdf_fs_read(fd, &b, 1);
        ok(b == 0x00, "and NOW it is the one just written");
        zzdf_fs_close(fd);
    }
    ok(zzdf_fs_mtime("quicksave.tfe") > zzdf_fs_mtime("quicksave2.tfe"),
       "a save written this session is newer than any staged one");

     
    fd = zzdf_fs_open("/Saves/Dark Forces/quicksave2.tfe", 1, 1);
    zzdf_fs_write(fd, payload, 64);
    zzdf_fs_close(fd);
    ok(zzdf_fs_mtime("quicksave2.tfe") > zzdf_fs_mtime("quicksave.tfe"),
       "the second slot written is then the newer of the two");

    /* --- 5. overflow never corrupts the previous file -------------- */
    {
        static char big[ZZDF_RAMF_CHUNK + 4096];
        int r; unsigned char b = 0xFF;
        fd = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 1, 1);
        ok(fd >= 0, "an oversized save opens");
        zzdf_fs_write(fd, big, (unsigned int)sizeof(big));
        zzdf_fs_close(fd);
        r = zzdf_fs_open("/Saves/Dark Forces/quicksave.tfe", 0, 0);
        ok(r >= 0, "the name still opens after the overflow");
        if (r >= 0) { zzdf_fs_read(r, &b, 1); zzdf_fs_close(r); }
        ok(b == 0xE1, "and it is the GOOD staged save, not the truncated one");
        ok(zzdf_fs_mtime("quicksave.tfe") == 1000,
           "an overflowed file does not claim to be the newest either");
    }
}

int main(void)
{
    printf("ZZDarkForces MemFS host tests\n");
    printf("=============================\n");

    build_fixture();
    test_mount();
    test_mount_limit();
    test_open_case();
    test_read();
    test_seek();
    test_badfd();
    test_handles();
    test_gob_access();
    test_seed_from_manifest();
    test_writeback_table();
    test_write_capacity();
    test_savestates();

    printf("\n=============================\n");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
