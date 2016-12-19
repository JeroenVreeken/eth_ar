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
#ifndef _INCLUDE_SOUND_H_
#define _INCLUDE_SOUND_H_

#include <stdint.h>
#include <poll.h>
#include <stdbool.h>

int sound_out(int16_t *samples, int nr, bool left, bool right);
int sound_silence(void);
int sound_init(char *device, 
    void (*in_cb)(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r),
    int nr_out, int nr_in, int rate_out, int rate_in_l, int rate_in_r, int hw_rate);
int sound_poll_count_tx(void);
int sound_poll_fill_tx(struct pollfd *fds, int count);
bool sound_poll_out_tx(struct pollfd *fds, int count);
int sound_poll_count_rx(void);
int sound_poll_fill_rx(struct pollfd *fds, int count);
bool sound_poll_in_rx(struct pollfd *fds, int count);
int sound_rx(void);

#endif /* _INCLUDE_SOUND_H_ */
