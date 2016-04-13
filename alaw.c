/* A-law code from GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * PCM - A-Law conversion
 *   Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "alaw.h"
#include <stdint.h>

static int16_t alaw_to_s16 (uint8_t a_val)
{
	int t;
	int seg;

	a_val ^= 0x55;
	t = a_val & 0x7f;
	if (t < 16)
		t = (t << 4) + 8;
	else {
		seg = (t >> 4) & 0x07;
		t = ((t & 0x0f) << 4) + 0x108;
		t <<= seg - 1;
	}
	return ((a_val & 0x80) ? t : -t);
}

void alaw_decode(int16_t *samples, uint8_t *alaw, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		samples[i] = alaw_to_s16(alaw[i]);
	}
}


/*
 * s16_to_alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 * s16_to_alaw() accepts an 16-bit integer and encodes it as A-law data.
 *
 *              Linear Input Code       Compressed Code
 *      ------------------------        ---------------
 *      0000000wxyza                    000wxyz
 *      0000001wxyza                    001wxyz
 *      000001wxyzab                    010wxyz
 *      00001wxyzabc                    011wxyz
 *      0001wxyzabcd                    100wxyz
 *      001wxyzabcde                    101wxyz
 *      01wxyzabcdef                    110wxyz
 *      1wxyzabcdefg                    111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */

static int val_seg (int val)
{
	int r = 1;

	val >>= 8;
	if (val & 0xf0) {
		val >>= 4;
		r += 4;
	}
	if (val & 0x0c) {
		val >>= 2;
		r += 2;
	}
	if (val & 0x02)
		r += 1;
	return r;
}


static uint8_t s16_to_alaw (int16_t pcm_val)
{
	int seg;
	uint8_t mask;
	uint8_t aval;

	if (pcm_val >= 0) {
		mask = 0xD5;
	} else {
		mask = 0x55;
		pcm_val = -pcm_val;
		if (pcm_val > 0x7fff)
			pcm_val = 0x7fff;
	}

	if (pcm_val < 256)
		aval = pcm_val >> 4;
	else {
		/* Convert the scaled magnitude to segment number. */
		seg = val_seg (pcm_val);
		aval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0x0f);
	}
	return aval ^ mask;
}

void alaw_encode(uint8_t *alaw, int16_t *samples, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		alaw[i] = s16_to_alaw(samples[i]);
	}
}
