/*
 * prog.c - SYNTHETIC CPS-1 test program (tests only, no Capcom code, ASCII only).
 *
 * It programs the CPS-A / CPS-B registers the way a CPS-1 game does and builds a scene whose
 * every pixel is predictable from the MAME documentation of the hardware (tests/check_host.py
 * holds the independent expected model). Phases, in program frames:
 *    0.. : all layers, no scroll                        (checked around emulated frame 25)
 *   30.. : scroll offsets on the three tile layers      (frame 50)
 *   60.. : scroll 2 and 3 disabled, star fields visible (frame 80)
 *   90.. : all layers, row scroll on scroll 2           (frame 110)
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
#define W(a) (*(volatile u16 *)(a))
#define B(a) (*(volatile u8 *)(a))

volatile u16 vbl;
const u16 mid_table[2] __attribute__((section(".midrom"))) = { 0x1234, 0xABCD };
const u16 hi_table[2]  __attribute__((section(".hirom")))  = { 0x55AA, 0xC0DE };

#define CPSA  0x800100ul
#define CPSB  0x800140ul
#define OBJ   0x900000ul
#define SCR2  0x904000ul
#define SCR1  0x908000ul
#define SCR3  0x90C000ul
#define PAL   0x914000ul
#define OTHER 0x920000ul
#define RES   0xFF8000ul

#define LAYER_ORDER ((3u << 6) | (2u << 8) | (0u << 10) | (1u << 12))  /* back: s3, s2, obj, s1 */

static void wait_vbl(void) { u16 v = vbl; while (vbl == v) { } }

static void put_sprite(u16 n, u16 sx, u16 sy, u16 code, u16 attr)
{
    volatile u16 *p = (volatile u16 *)(OBJ + ((u32)n << 3));
    p[0] = sx + 64; p[1] = sy + 16; p[2] = code; p[3] = attr;
}

void main(void)
{
    volatile u16 *q;
    u16 p, g, c, row, col, f, i;

    W(CPSA + 0x00) = (u16)(OBJ >> 8);
    W(CPSA + 0x02) = (u16)(SCR1 >> 8);
    W(CPSA + 0x04) = (u16)(SCR2 >> 8);
    W(CPSA + 0x06) = (u16)(SCR3 >> 8);
    W(CPSA + 0x08) = (u16)(OTHER >> 8);
    for (i = 0x0C; i <= 0x20; i += 2) W(CPSA + i) = 0;
    W(CPSA + 0x22) = 0x000E;                         /* video control: scroll 2 + 3 on */
    W(CPSB + 0x28) = 0; W(CPSB + 0x2A) = 0; W(CPSB + 0x2C) = 0; W(CPSB + 0x2E) = 0;
    W(CPSB + 0x30) = 0x003F;                         /* palette control: all six pages */
    W(CPSB + 0x26) = LAYER_ORDER | 0x0E | 0x30;      /* layer control */

    q = (volatile u16 *)PAL;                         /* colour = f(page, group, pen) */
    for (p = 0; p < 6; p++)
        for (g = 0; g < 32; g++)
            for (c = 0; c < 16; c++)
                *q++ = 0xF000 | ((u16)(p + p + 3) << 8) | (c << 4) | (g & 15);

    for (row = 0; row < 64; row++) {
        for (col = 0; col < 64; col++) {
            q = (volatile u16 *)(SCR3 + (((u32)((row & 0x07) + (col << 3) + ((row & 0x38) << 6))) << 2));
            q[0] = (col + row) & 7; q[1] = 3;
            q = (volatile u16 *)(SCR2 + (((u32)((row & 0x0F) + (col << 4) + ((row & 0x30) << 6))) << 2));
            if (row == 4 || row == 5) { q[0] = 0x2000 + ((((col & 7) + 1)) << 2); q[1] = 2; }
            else { q[0] = 0x203C; q[1] = 2; }
            q = (volatile u16 *)(SCR1 + (((u32)((row & 0x1F) + (col << 5) + ((row & 0x20) << 6))) << 2));
            if (row == 6 || row == 7) { q[0] = 0x7000 + ((((col & 7) + 1)) << 3); q[1] = 1; }
            else { q[0] = 0x7078; q[1] = 1; }
        }
    }
    for (i = 0; i < 1024; i++) W(OTHER + ((u32)i << 1)) = i;   /* row scroll table */

    put_sprite(0, 100, 100, 0x021C, 0x0004);         /* solid pen 7 */
    put_sprite(1, 200, 100, 0x0240, 0x1105);         /* 2x2 block */
    put_sprite(2,  40, 150, 0x0100, 0x0006);         /* quadrant tile, four flips */
    put_sprite(3,  60, 150, 0x0100, 0x0026);
    put_sprite(4,  80, 150, 0x0100, 0x0046);
    put_sprite(5, 100, 150, 0x0100, 0x0066);
    put_sprite(6, 140, 150, 0x0104, 0x0006);         /* ramp tile, plain and x-flipped */
    put_sprite(7, 160, 150, 0x0104, 0x0026);
    put_sprite(8, 300,  40, 0x021C, 0x0004);         /* straddles scroll 1 and scroll 2 */
    put_sprite(9, 0, 0, 0, 0xFF00);                  /* end of list */

    W(RES + 12) = mid_table[0]; W(RES + 14) = mid_table[1];
    W(RES + 16) = hi_table[0];  W(RES + 18) = hi_table[1];
    W(RES + 0x20) = 0xC0FE;

    __asm__ volatile("move.w #0x2000,%sr");          /* interrupts on */
    for (f = 0;; f++) {
        wait_vbl();
        W(CPSA + 0x0A) = (u16)(PAL >> 8);            /* palette base: latches the palette */
        if (f == 10) B(0x800181ul) = 0x42;           /* sound command */
        if (f == 30) { W(CPSA + 0x0C) = 3; W(CPSA + 0x10) = 37; W(CPSA + 0x12) = 5; W(CPSA + 0x14) = 100; }
        if (f == 60) W(CPSB + 0x26) = LAYER_ORDER | 0x02 | 0x30;
        if (f == 90) { W(CPSB + 0x26) = LAYER_ORDER | 0x0E | 0x30; W(CPSA + 0x22) = 0x000F; }
        W(RES + 0) = W(0x800000ul); W(RES + 2) = W(0x800018ul);
        W(RES + 4) = W(0x80001Aul); W(RES + 6) = W(0x80001Cul); W(RES + 8) = W(0x80001Eul);
        W(RES + 10) = f;
    }
}
