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

struct emphasis {
	int16_t prev;
};

struct emphasis *emphasis_init(void)
{
	return calloc(1, sizeof(struct emphasis));
}

int emphasis_reset(struct emphasis *emphasis)
{
	emphasis->prev = 0;
	return 0;
}

int emphasis_pre(struct emphasis *emphasis, int16_t *sound, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		long sample = sound[i];
		
		sample -= emphasis->prev;
		
		emphasis->prev = sound[i];
		if (sample > 16535)
			sample = 16535;
		if (sample < -16536)
			sample = -16536;
		
		sound[i] = sample;
	}
	
	return 0;
}


