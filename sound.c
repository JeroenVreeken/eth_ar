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
#include "sound.h"

#include <endian.h>
#include <alsa/asoundlib.h>

/* Our device handle */
static snd_pcm_t *pcm_handle_tx = NULL;
static snd_pcm_t *pcm_handle_rx = NULL;
static void (*sound_in_cb)(int16_t *samples, int nr);

int sound_out(int16_t *samples, int nr)
{
	int r;
	
	r = snd_pcm_writei (pcm_handle_tx, samples, nr);
//	printf("alsa: %d\n", r);
	if (r < 0) {
		printf("recover output\n");
		snd_pcm_recover(pcm_handle_tx, r, 1);
		snd_pcm_writei (pcm_handle_tx, samples, nr);
	}

	return 0;
}

int sound_poll_count_tx(void)
{
	return snd_pcm_poll_descriptors_count(pcm_handle_tx);
}

int sound_poll_fill_tx(struct pollfd *fds, int count)
{
	if (snd_pcm_poll_descriptors(pcm_handle_tx, fds, count) >= 0)
		return 0;
	return -1;
}

bool sound_poll_out_tx(struct pollfd *fds, int count)
{
	unsigned short revents;
	
	snd_pcm_poll_descriptors_revents(pcm_handle_tx, fds, count, &revents);
	if (revents & POLLOUT)
		return true;
	else
		return false; 	
}

int sound_poll_count_rx(void)
{
	return snd_pcm_poll_descriptors_count(pcm_handle_rx);
}

int sound_poll_fill_rx(struct pollfd *fds, int count)
{
	if (snd_pcm_poll_descriptors(pcm_handle_rx, fds, count) >= 0)
		return 0;
	return -1;
}

bool sound_poll_in_rx(struct pollfd *fds, int count)
{
	unsigned short revents;
	
	snd_pcm_poll_descriptors_revents(pcm_handle_rx, fds, count, &revents);
	if (revents & POLLIN)
		return true;
	else
		return false; 	
}

static int nr;

int sound_rx(void)
{
	int r;
	int16_t samples[nr];
	
	r = snd_pcm_readi(pcm_handle_rx, samples, nr);
	
	if (r > 0) {
		sound_in_cb(samples, r);
	} else {
		printf("recover input (nr=%d, r=%d)\n", nr, r);
		snd_pcm_recover(pcm_handle_rx, r, 0);
		snd_pcm_start(pcm_handle_rx);
	}
	
	return 0;
}

int sound_param(snd_pcm_t *pcm_handle, int rate)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_hw_params_malloc (&hw_params);

	snd_pcm_hw_params_any(pcm_handle, hw_params);
	snd_pcm_hw_params_set_access (pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);

	if (htole16(0x1234) == 0x1234)
		snd_pcm_hw_params_set_format (pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
	else
		snd_pcm_hw_params_set_format (pcm_handle, hw_params, SND_PCM_FORMAT_S16_BE);
	
	unsigned int rrate = rate;
	
	snd_pcm_hw_params_set_rate_near (pcm_handle, hw_params, &rrate, NULL);
	snd_pcm_hw_params_set_channels (pcm_handle, hw_params, 1);

	snd_pcm_uframes_t buffer_size = nr * 10 * 100;
	snd_pcm_uframes_t period_size = nr * 10;

	snd_pcm_hw_params_set_buffer_size_near (pcm_handle, hw_params, &buffer_size);
	snd_pcm_hw_params_set_period_size_near (pcm_handle, hw_params, &period_size, NULL);

	snd_pcm_hw_params (pcm_handle, hw_params);

	snd_pcm_hw_params_free (hw_params);


	snd_pcm_sw_params_t *sw_params;

	snd_pcm_sw_params_malloc (&sw_params);
	snd_pcm_sw_params_current (pcm_handle, sw_params);

	snd_pcm_sw_params_set_start_threshold(pcm_handle, sw_params, buffer_size - period_size);
	snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, period_size);

	snd_pcm_sw_params(pcm_handle, sw_params);

	return 0;
}

int sound_init(char *device, void (*in_cb)(int16_t *samples, int nr), int inr, int rate)
{
	int err;

	/* The device name */
	const char *device_name;
	if (device)
		device_name = device;
	else
		device_name = "default"; 

	sound_in_cb = in_cb;
	nr = inr;

	/* Open the device */
	err = snd_pcm_open (&pcm_handle_tx, device_name, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0)
		return -1;

	sound_param(pcm_handle_tx, rate);

	err = snd_pcm_open (&pcm_handle_rx, device_name, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0)
		return -1;
	
	sound_param(pcm_handle_rx, rate);

	snd_pcm_prepare(pcm_handle_tx);
	snd_pcm_start(pcm_handle_rx);

	return 0;
}
