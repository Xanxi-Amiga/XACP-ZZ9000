 























#ifndef ZZDF_PCM_H
#define ZZDF_PCM_H

/* Clamp a mixed float to s16. Kept here with the packing because the
   two together define what lands in the ring. */
static inline int zzdf_pcm_clamp(float v)
{
	int s = (int)(v * 32767.0f);
	if (s >  32767) { s =  32767; }
	if (s < -32768) { s = -32768; }
	return s;
}

/* Store one stereo frame at byte offset pos, big-endian, L then R.
   Returns the next offset, wrapped. mask must be size-1 of a power of
   two ring. */
static inline unsigned int zzdf_pcm_put(volatile unsigned char *ring,
                                        unsigned int pos, unsigned int mask,
                                        int l, int r)
{
	ring[pos]                = (unsigned char)((l >> 8) & 0xFF);
	ring[(pos + 1u) & mask]  = (unsigned char)(l & 0xFF);
	ring[(pos + 2u) & mask]  = (unsigned char)((r >> 8) & 0xFF);
	ring[(pos + 3u) & mask]  = (unsigned char)(r & 0xFF);
	return (pos + 4u) & mask;
}

/* Bytes the reader has not consumed yet. Both positions are kept inside
   16 bits so each is published by a single half-word store through the
   window and can never be read torn; the subtraction wraps correctly on
   its own. */
static inline unsigned int zzdf_pcm_used(unsigned int wr, unsigned int rd,
                                         unsigned int mask)
{
	return (wr - rd) & mask;
}

#endif /* ZZDF_PCM_H */
