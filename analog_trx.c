/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016, 2017

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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/mman.h>

#include <codec2/codec2.h>
#include <speex/speex_preprocess.h>

#include "interface.h"
#include <eth_ar/eth_ar.h>
#include "sound.h"
#include "eth_ar/alaw.h"
#include "eth_ar/ulaw.h"
#include "io.h"
#include "ctcss.h"
#include "eth_ar_codec2.h"

static bool verbose = false;

static struct CODEC2 *rx_codec;
static uint16_t rx_type;

static int nr_samples;
static int16_t *samples_rx;
static int nr_rx;

static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static struct sound_resample *sr_out = NULL;
static struct sound_resample *sr_in = NULL;
static struct iir *iir;

SpeexPreprocessState *st;

struct tx_packet {
	int16_t *samples;
	int nr_samples;
	
	struct tx_packet *next;
};

static struct tx_packet *queue_voice = NULL;

static bool energy_squelch = false;


float samples_get_energy(short *samples, int nr)
{
	float e = 0;
	int i;
	
	for (i = 0; i < nr; i++) {
		e += (float)(samples[i] * samples[i]) /  (8192);
	}
	e /= nr;
	
	return e;
}

static int energy_squelch_value = 0;
#define ENERGY_SQUELCH_THRESHOLD 15.0
#define ENERGY_SQUELCH_OPEN 4000

static bool energy_squelch_state(double energy, int nr)
{
	bool old_state = energy_squelch_value;
	bool new_state;
	
	if (energy >= ENERGY_SQUELCH_THRESHOLD) {
		energy_squelch_value = ENERGY_SQUELCH_OPEN;
		new_state = true;
	} else {
		energy_squelch_value -= nr;
		if (energy_squelch_value < 0)
			energy_squelch_value = 0;
		new_state = energy_squelch_value;
	}
	
	if (new_state != old_state) {
		printf("Energy squelch: %d\n", new_state);
	}
	
	return new_state;
}

static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	printf("Control: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl), 0, 1);
}


static void cb_sound_in(int16_t *hw_samples, int16_t *samples_r, int hw_nr, int nr_r)
{
	bool rx_state = io_state_rx_get();
	if (!rx_state && !energy_squelch)
		return;

	int nr;
	int16_t *samples;

	if (st)
		speex_preprocess_run(st, hw_samples);

	if (sr_in) {
		nr = sound_resample_nr_out(sr_in, hw_nr);
		samples = alloca(sizeof(int16_t) * nr);
//		sound_resample_perform(sr_in, samples, hw_samples, nr, hw_nr);
		sound_resample_perform_gain_limit(sr_in, samples, hw_samples, nr, hw_nr, 2.0);
	} else {
		nr = hw_nr;
		samples = hw_samples;
	}

	if (iir) {
		filter_iir_2nd(iir, samples, nr);
	}

	if (rx_codec) {
		while (nr) {
			int copy = nr_samples - nr_rx;
			if (copy > nr)
				copy = nr;

			memcpy(samples_rx + nr_rx, samples, copy * 2);
			samples += copy;
			nr -= copy;
			nr_rx += copy;
		
			if (nr_rx == nr_samples) {
				int bytes_per_codec_frame = (codec2_bits_per_frame(rx_codec) + 7)/8;
				unsigned char packed_codec_bits[bytes_per_codec_frame];
				
				codec2_encode(rx_codec, packed_codec_bits, samples_rx);
				
				bool pass = rx_state;
				if (energy_squelch) {
					double energy = codec2_get_energy(rx_codec, packed_codec_bits);
					pass |= energy_squelch_state(energy, nr_samples);
				}
				if (pass)
					interface_rx(bcast, mac, rx_type, packed_codec_bits, bytes_per_codec_frame, 0, 1);

				nr_rx = 0;
			}
		}
	} else {
		double energy = samples_get_energy(samples, nr);

		if (rx_state || (energy_squelch && energy_squelch_state(energy, nr))) {
			uint8_t alaw[nr];
		
			alaw_encode(alaw, samples, nr);
		
			interface_rx(bcast, mac, ETH_P_ALAW, alaw, nr, 0, 1);
		}
	}
}

static void queue_add(int16_t *samples, int nr_samples)
{
	struct tx_packet *p, **q;
	
	p = malloc(sizeof(struct tx_packet));
	if (!p)
		return;
	
	p->samples = malloc(sizeof(uint16_t) * nr_samples);
	if (!p->samples) {
		free(p);
		return;
	}
	
	p->next = NULL;
	p->nr_samples = nr_samples;
	memcpy(p->samples, samples, sizeof(uint16_t) * nr_samples);
	
	for (q = &queue_voice; *q; q = &(*q)->next);
	*q = p;
}


static uint8_t *tx_data;
static size_t tx_data_len;
static int tx_mode = -1;
static struct CODEC2 *tx_codec = NULL;
static int tx_bytes_per_codec_frame = 8;


static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level)
{
	int newmode = 0;
	bool is_c2 = true;

	if (!eth_ar_eth_p_isvoice(eth_type))
		return 0;
	
	is_c2 = eth_ar_eth_p_iscodec2(eth_type);
	newmode = eth_ar_eth_p_codecmode(eth_type);
	
	if (is_c2 && newmode != tx_mode) {
		if (tx_codec)
			codec2_destroy(tx_codec);
		tx_codec = codec2_create(newmode);
		tx_mode = newmode;
		tx_data_len = 0;
		tx_bytes_per_codec_frame = (codec2_bits_per_frame(tx_codec) + 7)/8;
	}	
	
	if (is_c2) {
		while (len) {
			size_t copy = len;
			if (copy + tx_data_len > tx_bytes_per_codec_frame)
				copy = tx_bytes_per_codec_frame - tx_data_len;
			
			memcpy(tx_data + tx_data_len, data, copy);
			tx_data_len += copy;
			data += copy;
			len -= copy;
			
			if (tx_data_len == tx_bytes_per_codec_frame) {
				int nr = codec2_samples_per_frame(tx_codec);
				int16_t mod_out[nr];
				
				codec2_decode(tx_codec, mod_out, tx_data);
				
				if (nr > 0) {
					queue_add(mod_out, nr);
				}
				
				tx_data_len = 0;
			}
		}
	} else {
		int16_t mod_out[len];
		
		if (newmode == CODEC_MODE_ULAW)
			ulaw_decode(mod_out, data, len);
		else
			alaw_decode(mod_out, data, len);
		
		
		queue_add(mod_out, len);
	}

	return 0;
}

static int prio(void)
{
	struct sched_param param;
	param.sched_priority = 90;

	if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		printf("sched_setscheduler() failed: %s",
		    strerror(errno));
	}

	if (mlockall(MCL_CURRENT | MCL_FUTURE )) {
		printf("mlockall failed: %s",
		    strerror(errno));
	}
	return 0;
}




static void dequeue_voice(void)
{
	struct tx_packet *p = queue_voice;
	if (!p) {
		sound_silence();
	} else {
		queue_voice = p->next;
	
		if (!sr_out) {
			sound_out(p->samples, p->nr_samples, true, true);
		} else {
			int nr_out = sound_resample_nr_out(sr_out, p->nr_samples);
			int16_t hw_mod_out[nr_out];
					
			sound_resample_perform(sr_out, hw_mod_out, p->samples, nr_out, p->nr_samples);
			sound_out(hw_mod_out, nr_out, true, true);
		}

		free(p->samples);
		free(p);
	}
}

static void usage(void)
{
	printf("Options:\n");
	printf("-a\tUse A-Law encoding\n");
	printf("-b\tFilter voice band\n");
	printf("-c [call]\town callsign\n");
	printf("-e\tUse sample energy as voice squelch\n");
	printf("-I\tUse input device as toggle instead of keypress\n");
	printf("-i [dev]\tUse input device instead of DCD\n");
	printf("-M [mode]\tCodec2 mode\n");
	printf("-n [dev]\tNetwork device name (default: \"analog\")\n");
	printf("-r [rate]\tSound rate\n");
	printf("-S\tUse socket on existing network device\n");
	printf("-s [dev]\tSound device (default: \"default\")\n");
	printf("-v\tverbose\n");
}

int main(int argc, char **argv)
{
	char *call = "pirate";
	char *sounddev = "default";
	char *netname = "analog";
	char *inputdev = NULL;
	bool inputtoggle = false;
	int fd_int;
	struct pollfd *fds;
	int sound_fdc_tx;
	int sound_fdc_rx;
	int nfds;
	int poll_int;
	int io_fdc;
	int poll_io;
	int opt;
	int mode = CODEC2_MODE_3200;
	bool is_c2 = true;
	bool tap = true;
	int a_rate = 8000;
	int rate = a_rate;
	int nr_sound;
	bool denoise = true;

	while ((opt = getopt(argc, argv, "abc:eIi:M:n:r:Ss:v")) != -1) {
		switch(opt) {
			case 'a':
				is_c2 = false;
				break;
			case 'b':
				iir = filter_iir_create_8k_hp_300hz();
				break;
			case 'c':
				call = optarg;
				break;
			case 'e':
				energy_squelch = true;
				break;
			case 'I':
				inputtoggle = true;
				break;
			case 'i':
				inputdev = optarg;
				break;
			case 'M':
				if (!strcmp(optarg, "3200")) {
					mode = CODEC2_MODE_3200;
				} else if (!strcmp(optarg, "2400")) {
					mode = CODEC2_MODE_2400;
				} else if (!strcmp(optarg, "1600")) {
					mode = CODEC2_MODE_1600;
				} else if (!strcmp(optarg, "1400")) {
					mode = CODEC2_MODE_1400;
				} else if (!strcmp(optarg, "1300")) {
					mode = CODEC2_MODE_1300;
				} else if (!strcmp(optarg, "1200")) {
					mode = CODEC2_MODE_1200;
#if defined(CODEC2_MODE_700)
				} else if (!strcmp(optarg, "700")) {
					mode = CODEC2_MODE_700;
#endif
#if defined(CODEC2_MODE_700B)
				} else if (!strcmp(optarg, "700B")) {
					mode = CODEC2_MODE_700B;
#endif
				} else if (!strcmp(optarg, "700C")) {
					mode = CODEC2_MODE_700C;
				} else if (!strcmp(optarg, "450")) {
					mode = CODEC2_MODE_450;
				} else if (!strcmp(optarg, "450PWB")) {
					mode = CODEC2_MODE_450PWB;
				}
				break;
			case 'n':
				netname = optarg;
				break;
			case 'r':
				rate = atoi(optarg);
				break;
			case 'S':
				tap = false;
				break;
			case 's':
				sounddev = optarg;
				break;
			case 'v':
				verbose = true;
				break;
			default:
				usage();
				return -1;
		}
	}
	
	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		return -1;
	}
	
	if (is_c2) {
		rx_codec = codec2_create(mode);
		nr_samples = codec2_samples_per_frame(rx_codec);
		switch(mode) {
			case CODEC2_MODE_3200:
				rx_type = ETH_P_CODEC2_3200;
				break;
			case CODEC2_MODE_2400:
				rx_type = ETH_P_CODEC2_2400;
				break;
			case CODEC2_MODE_1600:
				rx_type = ETH_P_CODEC2_1600;
				break;
			case CODEC2_MODE_1400:
				rx_type = ETH_P_CODEC2_1400;
				break;
			case CODEC2_MODE_1300:
				rx_type = ETH_P_CODEC2_1300;
				break;
			case CODEC2_MODE_1200:
				rx_type = ETH_P_CODEC2_1200;
				break;
#if defined(CODEC2_MODE_700)
			case CODEC2_MODE_700:
				rx_type = ETH_P_CODEC2_700;
				break;
#endif
#if defined(CODEC2_MODE_700B)
			case CODEC2_MODE_700B:
				rx_type = ETH_P_CODEC2_700B;
				break;
#endif
			case CODEC2_MODE_700C:
				rx_type = ETH_P_CODEC2_700C;
				break;
#ifdef CODEC2_MODE_1300C
			case CODEC2_MODE_1300C:
				rx_type = ETH_P_CODEC2_1300C;
				break;
#endif
		}
	} else {
		rx_codec = NULL;
		nr_samples = 160;
	}
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));
	tx_data = calloc(16, sizeof(uint8_t));

	fd_int = interface_init(netname, mac, tap, 0);
	if ((rate = sound_init(sounddev, cb_sound_in, rate, 0, 0)) < 0) {
		printf("Could not open sound device\n");
		return -1;
	} else {
		printf("Sound rate: %d\n", rate);
	}
	nr_sound = nr_samples * rate / a_rate;
	sound_set_nr(nr_sound);

	if (denoise) {
		int val;
		float fval;
		st = speex_preprocess_state_init(nr_sound, rate);
		val= denoise;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, &val);
		val = -30;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &val);

		val=1;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC, &val);
		fval=32768;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_LEVEL, &fval);
		
		val = 40;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &val);
		
 		val=60;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &val);
	} else {
		printf("No denoise\n");
	}
	
	if (a_rate != rate) {
		sr_out = sound_resample_create(rate, a_rate);
		sr_in = sound_resample_create(a_rate, rate);
	}

	if (inputdev)
		io_init_input(inputdev, inputtoggle);
	io_init_tty();

	prio();
	
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	sound_fdc_tx = sound_poll_count_tx();
	sound_fdc_rx = sound_poll_count_rx();
	nfds = sound_fdc_tx + sound_fdc_rx + 1;
	io_fdc = io_fs_nr();
	nfds += io_fdc;
	fds = calloc(sizeof(struct pollfd), nfds);
	
	sound_poll_fill_tx(fds, sound_fdc_tx);
	sound_poll_fill_rx(fds + sound_fdc_tx, sound_fdc_rx);
	poll_int = sound_fdc_tx + sound_fdc_rx;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;
	poll_io = poll_int + 1;
	io_poll_fill(fds + poll_io, io_fdc);


	do {
		poll(fds, nfds, -1);
		if (sound_poll_out_tx(fds, sound_fdc_tx)) {
			dequeue_voice();
		}
		if (sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
		}
		io_handle(fds + poll_io, io_fdc, cb_control);
	} while (1);
	
	
	return 0;
}
