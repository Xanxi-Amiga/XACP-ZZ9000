/*
 * zzvr_hle_diag.h - ZZVirtuaRacingRIX: what VRDE's hle.c printed on stderr
 * becomes a counter in the shared slots (no console on the Core1).
 * ASCII only.
 */
#ifndef ZZVR_HLE_DIAG_H
#define ZZVR_HLE_DIAG_H

#define ZZVR_HLE_NOTE_VERTICES   1   /* "model with %d vertices" (unexpected model size) */
#define ZZVR_HLE_NOTE_DEPTH_MODE 2   /* "depth mode %x" (unknown depth mode)            */

void zzvr_hle_note(int what, int value);

#endif
