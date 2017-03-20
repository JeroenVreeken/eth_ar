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

static int channels_out = 1;
static int channels_in = 1;

struct sound_resample {
	SRC_STATE *src;
	int rate_in;
	int rate_out;
	double ratio;
};

struct sound_resample *sound_resample_create(int rate_out, int rate_in)
{
	int err;

	struct sound_resample *sr = calloc(1, sizeof(struct sound_resample));
	if (!sr)
		goto err_sr;

	sr->src = src_new(SRC_LINEAR, 1, &err);
	if (!sr->src)
		goto err_src;

	sr->ratio = (double)rate_out / (double)rate_in;

	return sr;

err_src:
	free(sr);
err_sr:
	return NULL;	
}

void sound_resample_destroy(struct sound_resample *sr)
{
	if (!sr)
		return;
	
	src_delete(sr->src);
	free(sr);
}

int sound_resample_perform(struct sound_resample *sr, int16_t *out, int16_t *in, int nr_out, int nr_in)
{
	float fl_in[nr_in], fl_out[nr_out];
	SRC_DATA data;
	data.data_in = fl_in;
	data.data_out = fl_out;
	data.input_frames = nr_in;
	data.output_frames = nr_out;
	data.end_of_input = 0;
	data.src_ratio = sr->ratio;
	
	src_short_to_float_array(in, fl_in, nr_in);
	src_process(sr->src, &data);
	src_float_to_short_array(fl_out, out, nr_out);
	
	return 0;
}

int sound_resample_nr_out(struct sound_resample *sr, int nr_in)
{
	return nr_in * sr->ratio;
}

int sound_resample_nr_in(struct sound_resample *sr, int nr_out)
{
	return nr_out / sr->ratio;
}

int written;
int failed;

static int sound_out_alsa(int16_t *play_samples, int nr)
{
	int r;
	
	r = snd_pcm_writei (pcm_handle_tx, play_samples, nr);
//	printf("alsa: %d\n", r);
	if (r < 0) {
		failed++;
		printf("recover output %d %d %d\n", written, failed, written/failed);
		snd_pcm_recover(pcm_handle_tx, r, 1);
		snd_pcm_writei (pcm_handle_tx, play_samples, nr);
	}
	written++;

	return 0;
}

int sound_out_lr(int16_t *samples_l, int16_t *samples_r, int nr)
{
	int16_t samples[nr];
	int i;
	
	for (i = 0; i < nr; i++) {
		samples[i * 2 + 0] = samples_l[i];
		samples[i * 2 + 1] = samples_r[i];
	}

	return sound_out_alsa(samples, nr);
}

int sound_out(int16_t *samples, int nr, bool left, bool right)
{
	int16_t *play_samples;
	int16_t samples_2[nr * channels_out];
	int i;
	
	if (channels_out == 2) {
		/* Output is 2 channels */
		for (i = nr; i >= 0; i--) {
			if (left) 
				samples_2[i * 2 + 0] = samples[i];
			if (right)
				samples_2[i * 2 + 1] = samples[i];
		}
		play_samples = samples_2;
	} else {
		play_samples = samples;
	}
	
	return sound_out_alsa(play_samples, nr);
}

int16_t *silence = NULL;
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
	int rec_nr = nr;
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

	sound_in_cb(samples_l, samples_r, r, r);
	
	return 0;
}

int sound_param(snd_pcm_t *pcm_handle, bool is_tx, int hw_rate, int force_channels)
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
	
	unsigned int rrate = hw_rate;
	
	if (snd_pcm_hw_params_set_rate_near (pcm_handle, hw_params, &rrate, NULL)) {
		printf("Could not set rate %d\n", rrate);
	}
	printf("requested rate: %d got rate: %d\n", hw_rate, rrate);
	
	if (channels == 1 && snd_pcm_hw_params_set_channels (pcm_handle, hw_params, 1)) {
		printf("Could not set channels to 1\n");
		if (!force_channels)
			channels = 2;
		else
			channels = 0;
	}
	if (channels == 2 && snd_pcm_hw_params_set_channels (pcm_handle, hw_params, 2)) {
		printf("Could not set channels to 2\n");
		channels = 0;
	}
	printf("Channels: %d\n", channels);
	if (!channels)
		return -1;
	if (is_tx) {
		channels_out = channels;
	} else {
		channels_in = channels;
	}

	snd_pcm_uframes_t buffer_size = rrate / 5;
	snd_pcm_uframes_t period_size = rrate / 50;

	snd_pcm_hw_params_set_buffer_size_near (pcm_handle, hw_params, &buffer_size);
	snd_pcm_hw_params_set_period_size_near (pcm_handle, hw_params, &period_size, NULL);

	if (snd_pcm_hw_params(pcm_handle, hw_params)) {
		printf("Could not set HW params\n");
		return -1;
	}

	snd_pcm_hw_params_free (hw_params);


	return rrate;
}


int sound_buffer(snd_pcm_t *pcm_handle, int buffer_nr, bool is_tx)
{
	snd_pcm_uframes_t period_size = buffer_nr * 1;

	snd_pcm_sw_params_t *sw_params;

	snd_pcm_sw_params_malloc (&sw_params);
	snd_pcm_sw_params_current (pcm_handle, sw_params);

	snd_pcm_sw_params_set_start_threshold(pcm_handle, sw_params, period_size);
	snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, period_size);

	if (snd_pcm_sw_params(pcm_handle, sw_params)) {
		printf("Could not set SW params\n");
		return -1;
	}

	snd_pcm_sw_params_free(sw_params);
	
	return 0;
}

int sound_init(char *device, 
    void (*in_cb)(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r),
    int hw_rate, int force_channels_in, int force_channels_out)
{
	int err;
	int rrate_tx, rrate_rx;

	/* The device name */
	const char *device_name;
	if (device)
		device_name = device;
	else
		device_name = "default"; 

	sound_in_cb = in_cb;

	/* Open the device */
	err = snd_pcm_open (&pcm_handle_tx, device_name, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		printf("snd_pcm_open() err %d: %s\n", err, snd_strerror(err));
		return -1;
	}

	if ((rrate_tx = sound_param(pcm_handle_tx, true, hw_rate, force_channels_out)) < 0)
		return -1;

	err = snd_pcm_open (&pcm_handle_rx, device_name, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		printf("snd_pcm_open() err %d: %s\n", err, snd_strerror(err));
		return -1;
	}
	
	if ((rrate_rx = sound_param(pcm_handle_rx, false, hw_rate, force_channels_in)) < 0)
		return -1;

	if (rrate_rx != rrate_tx) {
		printf("TX and RX sample rate do not match\n");
		return -1;
	}

	return rrate_rx;
}

int sound_set_nr(int nr_set)
{
	nr = nr_set;

	if (sound_buffer(pcm_handle_tx, nr_set, true)) {
		printf("Could not set sound settings for TX\n");
		return -1;
	}
	if (sound_buffer(pcm_handle_rx, nr_set, false)) {
		printf("Could not set sound settings for RX\n");
		return -1;
	}

	silence_nr = nr_set;
	free(silence);
	silence = calloc(silence_nr * 2, sizeof(int16_t));

	snd_pcm_start(pcm_handle_rx);
	snd_pcm_prepare(pcm_handle_tx);

	return 0;	
}
