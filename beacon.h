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

#ifndef _INCLUDE_BEACON_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct beacon *beacon_init(int rate, int state_interval, int beacon_interval, char *message);
void beacon_destroy(struct beacon *beacon);

bool beacon_state_check(struct beacon *beacon);

int beacon_generate(struct beacon *beacon, int16_t *sound, int nr);
int beacon_generate_add(struct beacon *beacon, int16_t *sound, int nr);

struct beacon_sample {
	size_t nr;
	int16_t *samples;
};

struct beacon_sample *beacon_beep_create(int rate, double f, double t_off, double t_on, double amp);

#endif /* _INCLUDE_BEACON_H_ */
