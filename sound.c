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

#include <samplerate.h>

/* Our device handle */
static snd_pcm_t *pcm_handle_tx = NULL;
static snd_pcm_t *pcm_handle_rx = NULL;
static void (*sound_in_cb)(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r);

static SRC_STATE *src_out = NULL;
static SRC_STATE *src_in = NULL;

static double ratio_out = 1.0;
static double ratio_in = 1.0;

static int channels_out = 1;
static int channels_in = 1;

int written;
int failed;
int sound_out(int16_t *samples, int nr, bool left, bool right)
{
	int r;
	int play_nr = nr * ratio_out;
	int16_t play_samples[play_nr * channels_out];
	int i;
	
	if (src_out) {
		float data_in[nr], data_out[play_nr];
		SRC_DATA data;
		data.data_in = data_in;
		data.data_out = data_out;
		data.input_frames = nr;
		data.output_frames = play_nr;
		data.end_of_input = 0;
		data.src_ratio = ratio_out;

		src_short_to_float_array(samples, data_in, nr);
		
		src_process(src_out, &data);
		
		src_float_to_short_array(data_out, play_samples, play_nr);


		samples = play_samples;
		nr = play_nr;
	}

	/* Output is always multiple channels */
	for (i = play_nr; i >= 0; i--) {
		if (left) 
			play_samples[i * 2 + 0] = samples[i];
		if (right)
			play_samples[i * 2 + 1] = samples[i];
	}
	
	r = snd_pcm_writei (pcm_handle_tx, play_samples, nr);
//	printf("alsa: %d\n", r);
	if (r < 0) {
		failed++;
		printf("recover output %d %d %d\n", written, failed, written/failed);
		snd_pcm_recover(pcm_handle_tx, r, 1);
		snd_pcm_writei (pcm_handle_tx, samples, nr);
	}
	written++;

	return 0;
}

int16_t *silence;
int silence_nr;

int sound_silence(void)
{
	int r;
	
	r = snd_pcm_writei (pcm_handle_tx, silence, silence_nr);
//	printf("alsa: %d\n", r);
	if (r < 0) {
		printf("recover output\n");
		snd_pcm_recover(pcm_handle_tx, r, 1);
		snd_pcm_writei (pcm_handle_tx, silence, silence_nr);
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
	if (revents & (POLLOUT | POLLERR))
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
	if (revents & (POLLIN | POLLERR))
		return true;
	else
		return false; 	
}

static int nr;

int sound_rx(void)
{
	int i;
	int r;
	int rec_nr = nr * 1.0/ratio_in;
	int16_t rec_samples[rec_nr * channels_in];
	int16_t rec_samples_l[rec_nr];
	int16_t rec_samples_r[rec_nr];
	int16_t *samples_l, *samples_r;
	
	r = snd_pcm_readi(pcm_handle_rx, rec_samples, rec_nr);
	
	if (r <= 0) {
		printf("recover input (nr=%d, r=%d)\n", nr, r);
		snd_pcm_recover(pcm_handle_rx, r, 0);
		snd_pcm_start(pcm_handle_rx);
		
		return -1;
	}
	if (channels_in == 2) {
		for (i = 0; i < r; i++) {
			rec_samples_l[i] = rec_samples[i * 2];
			rec_samples_r[i] = rec_samples[i * 2 + 1];
		}
		samples_l = rec_samples_l;
		samples_r = rec_samples_r;
	} else {
		samples_l = rec_samples;
		samples_r = rec_samples;
	}

	if (src_in) {
		int r_in = r * ratio_in;
		float data_in[r], data_out[r_in];
		SRC_DATA data;
		data.data_in = data_in;
		data.data_out = data_out;
		data.input_frames = r;
		data.output_frames = r_in;
		data.end_of_input = 0;
		data.src_ratio = ratio_in;
		int16_t samples[r_in];

		src_short_to_float_array(samples_l, data_in, r);
		
		src_process(src_in, &data);
		
		src_float_to_short_array(data_out, samples, r_in);

		sound_in_cb(samples, samples_r, r_in, r);
	} else {
		sound_in_cb(samples_l, samples_r, r, r);
	}
	
	return 0;
}

int sound_param(snd_pcm_t *pcm_handle, bool is_tx, int sw_rate, int hw_rate, int force_channels)
{
	int channels = !force_channels ? 1 : force_channels;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_hw_params_malloc (&hw_params);

	snd_pcm_hw_params_any(pcm_handle, hw_params);
	if (snd_pcm_hw_params_set_access (pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) {
		printf("Interleaved not supported\n");
	}

	if (htole16(0x1234) == 0x1234)
		snd_pcm_hw_params_set_format (pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
	else
		snd_pcm_hw_params_set_format (pcm_handle, hw_params, SND_PCM_FORMAT_S16_BE);
	
	unsigned int rate = sw_rate;
	unsigned int rrate = hw_rate;
	
	if (snd_pcm_hw_params_set_rate_near (pcm_handle, hw_params, &rrate, NULL)) {
		printf("Could not set rate %d\n", rrate);
	}
	printf("rate: %d got rate: %d\n", rate, rrate);
	if (!is_tx && rrate != hw_rate) {
		printf("is_tx: %d, rrate: %d, hw_rate: %d\n", is_tx, rrate, hw_rate);
		return -1;
	}
	
	if (is_tx || channels == 1 || snd_pcm_hw_params_set_channels (pcm_handle, hw_params, 1)) {
		if (!is_tx && channels == 1)
			printf("Could not set channels to 1\n");
		if (snd_pcm_hw_params_set_channels (pcm_handle, hw_params, 2)) {
			printf("Could not set channels to 2\n");
		} else {
			channels = 2;
		}
	}
	printf("Channels: %d\n", channels);
	if (rate != rrate) {
		int err;
		SRC_STATE *src = src_new(SRC_LINEAR, 1, &err);
		double ratio = (double)rate / (double)rrate;
		
		if (is_tx) {
			src_out = src;
			ratio_out = 1.0/ratio;
		} else {
			src_in = src;
			ratio_in = ratio;
		}
	}
	if (is_tx) {
		channels_out = channels;
	} else {
		channels_in = channels;
	}

	snd_pcm_uframes_t buffer_size = nr * ( is_tx ? 3 : 10);
	snd_pcm_uframes_t period_size = nr * 1;

	snd_pcm_hw_params_set_buffer_size_near (pcm_handle, hw_params, &buffer_size);
	snd_pcm_hw_params_set_period_size_near (pcm_handle, hw_params, &period_size, NULL);

	snd_pcm_hw_params (pcm_handle, hw_params);

	snd_pcm_hw_params_free (hw_params);


	snd_pcm_sw_params_t *sw_params;

	snd_pcm_sw_params_malloc (&sw_params);
	snd_pcm_sw_params_current (pcm_handle, sw_params);

	snd_pcm_sw_params_set_start_threshold(pcm_handle, sw_params, period_size);
	snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, period_size);

	snd_pcm_sw_params(pcm_handle, sw_params);

	return 0;
}

int sound_init(char *device, 
    void (*in_cb)(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r),
    int nr_out, int nr_in, int rate_out, int rate_in_l, int rate_in_r, int hw_rate)
{
	int err;

	/* The device name */
	const char *device_name;
	if (device)
		device_name = device;
	else
		device_name = "default"; 

	sound_in_cb = in_cb;
	nr = nr_in;

	/* Open the device */
	err = snd_pcm_open (&pcm_handle_tx, device_name, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0)
		return -1;

	if (sound_param(pcm_handle_tx, true, rate_out, hw_rate, 0))
		return -1;

	err = snd_pcm_open (&pcm_handle_rx, device_name, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0)
		return -1;
	
	if (sound_param(pcm_handle_rx, false, rate_in_l, rate_in_r ? rate_in_r : hw_rate, rate_in_r ? 2 :0))
		return -1;

	silence_nr = nr_out * ratio_out;
	silence = calloc(silence_nr * 2, sizeof(int16_t));

	snd_pcm_prepare(pcm_handle_tx);
	snd_pcm_start(pcm_handle_rx);

	return 0;
}
