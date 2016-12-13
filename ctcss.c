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

#include "ctcss.h"

#include <math.h>

struct ctcss {
	int tone_nr;
	int16_t *tone;
	int cur;
};

struct ctcss *ctcss_init(int rate, double f, double amp)
{
	struct ctcss *ctcss;
	
	ctcss = calloc(1, sizeof(struct ctcss));
	if (!ctcss)
		return NULL;
	
	ctcss->tone_nr = rate * 10;
	ctcss->tone = calloc(ctcss->tone_nr, sizeof(int16_t));
	if (!ctcss->tone)
		return NULL;
	
	int i;
	
	for (i = 0; i < ctcss->tone_nr; i++) {
		ctcss->tone[i] = sin((2*M_PI * f * i) / (double)(rate)) * amp * 16535.0;
	}
	
	return ctcss;
}

int ctcss_add(struct ctcss *ctcss, int16_t *sound, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		long sample = sound[i];
		
		sample += ctcss->tone[ctcss->cur];
		
		if (sample > 16535)
			sample = 16535;
		if (sample < -16536)
			sample = -16536;
		
		sound[i] = sample;
		
		ctcss->cur = (ctcss->cur + 1) % ctcss->tone_nr;
	}
	
	return 0;
}

int ctcss_reset(struct ctcss *ctcss)
{
	ctcss->cur = 0;

	return 0;
}

