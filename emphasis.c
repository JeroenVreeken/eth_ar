/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "emphasis.h"
#include <math.h>

struct emphasis {
	float prev_de;
	int16_t prev_pre;
};

struct emphasis *emphasis_init(void)
{
	return calloc(1, sizeof(struct emphasis));
}

void emphasis_destroy(struct emphasis *emphasis)
{
	if (!emphasis)
		return;
	
	free(emphasis);
}

int emphasis_reset(struct emphasis *emphasis)
{
	emphasis->prev_pre = 0;
	emphasis->prev_de = 0;
	return 0;
}

// Set factor to 0.5 for 0db @ 3kHz (3kHz-4kHz is flattened)
// Set factor to 1.0 for 0db @ 1.5kHz (1.33kHz implementation)
#define AFACTOR_PRE 1.0
#define AFACTOR_DE  (1.0/AFACTOR_PRE)

int emphasis_pre(struct emphasis *emphasis, int16_t *sound, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		float sample = sound[i];
		
		sample -= emphasis->prev_pre;
		emphasis->prev_pre = sound[i];

		sample *= AFACTOR_PRE;
 
		if (sample > 32767)
			sample = 32767;
		if (sample < -32768)
			sample = -32768;
		
		sound[i] = sample;
	}
	
	return 0;
}

int emphasis_de(struct emphasis *emphasis, int16_t *sound, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		float sample = sound[i];
		
		sample += emphasis->prev_de;
		emphasis->prev_de = sample;
		
		sample *= AFACTOR_DE;

		if (sample > 32767)
			sample = 32767;
		if (sample < -32768)
			sample = -32768;

		sound[i] = sample;
	}
	
	return 0;
}

/* 48kHz de emphasis used before transmitting */
int emphasis_prede_48_gain(struct emphasis *emphasis, int16_t *sound, int nr, double gain)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		float sample = sound[i] / 32767.0;
		sample *= gain;
		float f = emphasis->prev_de;

		float absf = fabsf(i) / 100;
		f = f * (1.0 - absf);

		f += sample / 32;
		
		if (f > 1.0)
			f = 1.0;
		if (f < -1.0)
			f = -1.0;
		
		emphasis->prev_de = f;
		sound[i] = f * 32767.0;
	}
	
	return 0;
}
