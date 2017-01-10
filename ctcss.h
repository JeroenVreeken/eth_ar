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

#ifndef _INCLUDE_CTCSS_H_
#define _INCLUDE_CTCSS_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct ctcss *ctcss_init(int rate, double f, double amp);
void ctcss_destroy(struct ctcss *ctcss);

int ctcss_reset(struct ctcss *ctcss);

int ctcss_add(struct ctcss *ctcss, int16_t *sound, int nr);


/* detection code in dsp.c */

int ctcss_detect_init(double freq);
bool ctcss_detect_rx(short *smp, int nr);

struct iir;

struct iir *filter_iir_create_8k_hp_300hz();
int filter_iir_2nd(struct iir *iir, short *smp, int nr);

#endif /* _INCLUDE_CTCSS_H_ */
