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

#ifndef _INCLUDE_EMPHASIS_H_
#define _INCLUDE_EMPHASIS_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct emphasis *emphasis_init(void);
void emphasis_destroy(struct emphasis *emphasis);

int emphasis_reset(struct emphasis *emphasis);

int emphasis_pre(struct emphasis *emphasis, int16_t *sound, int nr);
int emphasis_de(struct emphasis *emphasis, int16_t *sound, int nr);

int emphasis_prede_48_gain(struct emphasis *emphasis, int16_t *sound, int nr, double gain);

#endif /* _INCLUDE_EMPHASIS_H_ */
