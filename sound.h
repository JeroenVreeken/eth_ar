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
int sound_out_lr(int16_t *samples_l, int16_t *samples_r, int nr);
int sound_silence(void);
/* Returns hw_rate or negative error*/
int sound_init(char *device, 
    void (*in_cb)(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r),
    int hw_rate, int force_channels_in, int force_channels_out);
int sound_set_nr(int nr_set);
int sound_poll_count_tx(void);
int sound_poll_fill_tx(struct pollfd *fds, int count);
bool sound_poll_out_tx(struct pollfd *fds, int count);
int sound_poll_count_rx(void);
int sound_poll_fill_rx(struct pollfd *fds, int count);
bool sound_poll_in_rx(struct pollfd *fds, int count);
int sound_rx(void);

struct sound_resample;

struct sound_resample *sound_resample_create(int rate_out, int rate_in);
void sound_resample_destroy(struct sound_resample *sr);
int sound_resample_perform(struct sound_resample *sr, int16_t *out, int16_t *in, int nr_out, int nr_in);
int sound_resample_perform_gain_limit(struct sound_resample *sr, int16_t *out, int16_t *in, int nr_out, int nr_in, float gain);
int sound_resample_nr_out(struct sound_resample *sr, int nr_in);
int sound_resample_nr_in(struct sound_resample *sr, int nr_out);

int sound_gain_limit(int16_t *samples, int nr, float gain, float *limit);

int sound_gain(int16_t *samples, int nr, double gain);

#endif /* _INCLUDE_SOUND_H_ */
