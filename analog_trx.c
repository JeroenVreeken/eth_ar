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
#include <hamlib/rig.h>

#include <codec2/codec2.h>

#include "interface.h"
#include "eth_ar.h"
#include "sound.h"
#include "dtmf.h"


static bool verbose = false;
static bool cdc = false;
static bool fullduplex = false;

int16_t *mod_silence;

struct CODEC2 *rx_codec;

int nr_samples;
int16_t *samples_rx;
int nr_rx;
int squelch_level_c = 10000000;
int squelch_level_o = 100000;
int squelch_on_delay = 3;
int squelch_off_delay = 10;

bool squelch(int16_t *samples, int nr)
{
	double total = 0;
	double high = 0, low = 0;
	int i;
	static int open_cnt = 0;
	static int close_cnt = 0;
	static bool state = false;
	
	for (i = 0; i < nr; i++) {
		total += samples[i] * samples[i];
		if (i & 1)
			high += samples[i];
		else
			high -= samples[i];
		if (i & 2)
			low += samples[i];
		else
			low -= samples[i];
	}
	high = high * high;
	low = low * low;
	
	if ((!state && high < squelch_level_o) ||
	    (state && high < squelch_level_c)) {
		open_cnt++;
		close_cnt = 0;
	} else {
		open_cnt = 0;
		close_cnt++;
	}
	if (!state && open_cnt >= squelch_on_delay) {
		printf("Open\t%f\t%f\t%f\n", high, low, high/low);
		state = true;
	}
	if (state && close_cnt >= squelch_off_delay) {
		printf("Close\t%f\t%f\t%f\n", high, low, high/low);
		state = false;
	}
	
//	static int cnt;
//	cnt++;
//	if ((cnt & 7) == 0)
//		printf("%f\t%f\t%f\n", high, low, high/low);
	return state;
}


static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(msg, strlen(ctrl), ETH_P_AR_CONTROL);
}


static void cb_sound_in(int16_t *samples, int nr)
{
	bool rx_state = squelch(samples, nr);

	dtmf_rx(samples, nr, cb_control);

	while (nr) {
		int copy = nr_samples - nr_rx;
		if (copy > nr)
			copy = nr;

		memcpy(samples_rx + nr_rx, samples, copy);
		samples += copy;
		nr -= copy;
		nr_rx += copy;
		
		if (nr_rx == nr_samples) {
			if (rx_state) {
				int bytes_per_codec_frame = (codec2_bits_per_frame(rx_codec) + 7)/8;
				unsigned char packed_codec_bits[bytes_per_codec_frame];
			
				codec2_encode(rx_codec, packed_codec_bits, samples_rx);
			
				interface_rx(packed_codec_bits, bytes_per_codec_frame,
				    ETH_P_CODEC2_3200);
			}
			nr_rx = 0;
		}
	}
}

uint8_t *tx_data;
size_t tx_data_len;
int tx_mode = -1;
struct CODEC2 *tx_codec = NULL;

static int cb_int_tx(uint8_t *data, size_t len, uint16_t eth_type)
{
	int newmode = 0;
	switch (eth_type) {
		case ETH_P_CODEC2_3200:
			newmode = CODEC2_MODE_3200;
			break;
		case ETH_P_CODEC2_2400:
			newmode = CODEC2_MODE_2400;
			break;
		case ETH_P_CODEC2_1600:
			newmode = CODEC2_MODE_1600;
			break;
		case ETH_P_CODEC2_1400:
			newmode = CODEC2_MODE_1400;
			break;
		case ETH_P_CODEC2_1300:
			newmode = CODEC2_MODE_1300;
			break;
		case ETH_P_CODEC2_1200:
			newmode = CODEC2_MODE_1200;
			break;
		case ETH_P_CODEC2_700:
			newmode = CODEC2_MODE_700;
			break;
		case ETH_P_CODEC2_700B:
			newmode = CODEC2_MODE_700B;
			break;
		default:
			return 0;
	}
	
	if (newmode != tx_mode) {
		if (tx_codec)
			codec2_destroy(tx_codec);
		tx_codec = codec2_create(newmode);
		tx_mode = newmode;
		tx_data_len = 0;
	}
	
	int bytes_per_codec_frame = (codec2_bits_per_frame(tx_codec) + 7)/8;
	
	while (len) {
		size_t copy = len;
		if (copy + tx_data_len > bytes_per_codec_frame)
			copy = bytes_per_codec_frame - tx_data_len;
		
		memcpy(tx_data + tx_data_len, data, copy);
		tx_data_len += copy;
		data += copy;
		len -= copy;
		
		if (tx_data_len == bytes_per_codec_frame) {
			int nr = codec2_samples_per_frame(tx_codec);
			int16_t mod_out[nr];
			
			codec2_decode(tx_codec, mod_out, tx_data);
			
			if (nr > 0) {
				sound_out(mod_out, nr);
			}
			
			tx_data_len = 0;
		}
	}

	return 0;
}

static int prio(void)
{
	struct sched_param param;

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


static RIG *rig;
static rig_model_t rig_model;
static char *ptt_file = NULL;
static ptt_type_t ptt_type = RIG_PTT_NONE;

static int hl_init(void)
{
	int retcode;
	
	rig = rig_init(rig_model);
	if (!rig) {
		printf("Could not init rig\n");
		return -1;
	}

	if (ptt_type != RIG_PTT_NONE)
		rig->state.pttport.type.ptt = ptt_type;

	if (ptt_file)
		strncpy(rig->state.pttport.pathname, ptt_file, FILPATHLEN - 1);

	retcode = rig_open(rig);
	if (retcode != RIG_OK) {
	  	fprintf(stderr,"rig_open: error = %s \n", rigerror(retcode));
		return -2;
	}

	return 0;
}

uint64_t tx_delay = 10000000;
uint64_t tx_tail = 100000000;
int tx_tail_ms = 100;
bool tx_state = false;
bool tx_started = false;
struct timespec tx_time;

void tx_delay_start(void)
{
	if (!tx_state) {
		if (!tx_started) {
			tx_started = true;
			if (verbose)
				printf("TX on\n");
			rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_ON);
			clock_gettime(CLOCK_MONOTONIC, &tx_time);
		} else {
			struct timespec now;
			
			clock_gettime(CLOCK_MONOTONIC, &now);
			uint64_t ontime = now.tv_sec - tx_time.tv_sec;
			ontime *= 1000000000;
			ontime += now.tv_nsec - tx_time.tv_nsec;
			
			if (ontime >= tx_delay) {
				if (verbose)
					printf("TX-delay done\n");
				tx_state = true;
			}
		}
	}
}

void tx_tail_extend(void)
{
	clock_gettime(CLOCK_MONOTONIC, &tx_time);
}

void tx_tail_check(void)
{
	if (tx_state) {
		struct timespec now;
		
		clock_gettime(CLOCK_MONOTONIC, &now);
		uint64_t ontime = now.tv_sec - tx_time.tv_sec;
		ontime *= 1000000000;
		ontime += now.tv_nsec - tx_time.tv_nsec;
			
		if (ontime >= tx_tail) {
			rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);
			tx_state = false;
			tx_started = false;
			if (verbose)
				printf("TX tail done\n");
		}
	}
}

static void usage(void)
{
	printf("Options:\n");
	printf("-v\tverbose\n");
	printf("-c [call]\town callsign\n");
	printf("-f\tfull-duplex\n");
	printf("-s [dev]\tSound device (default: \"default\")\n");
	printf("-n [dev]\tNetwork device name (default: \"freedv\")\n");
	printf("-m [model]\tHAMlib rig model\n");
	printf("-P [type]\tHAMlib PTT type\n");
	printf("-p [dev]\tHAMlib PTT device file\n");
	printf("-d [msec]\tTX delay\n");
	printf("-t [msec]\tTX tail\n");
}

int main(int argc, char **argv)
{
	char *call = "pirate";
	char *sounddev = "default";
	char *netname = "analog";
	int ssid = 0;
	uint8_t mac[6];
	int fd_int;
	struct pollfd *fds;
	int sound_fdc_tx;
	int sound_fdc_rx;
	int nfds;
	int poll_int;
	int opt;
	int mode = CODEC2_MODE_3200;
	
	rig_model = 1; // set to dummy.
	
	while ((opt = getopt(argc, argv, "vc:s:n:m:d:t:p:P:f")) != -1) {
		switch(opt) {
			case 'v':
				verbose = true;
				break;
			case 'c':
				call = optarg;
				break;
			case 's':
				sounddev = optarg;
				break;
			case 'n':
				netname = optarg;
				break;
			case 'm':
				rig_model = atoi(optarg);
				break;
			case 'p':
				ptt_file = optarg;
				break;
			case 'P':
				if (!strcmp(optarg, "RIG"))
					ptt_type = RIG_PTT_RIG;
				else if (!strcmp(optarg, "DTR"))
					ptt_type = RIG_PTT_SERIAL_DTR;
				else if (!strcmp(optarg, "RTS"))
					ptt_type = RIG_PTT_SERIAL_RTS;
				else if (!strcmp(optarg, "PARALLEL"))
					ptt_type = RIG_PTT_PARALLEL;
				else if (!strcmp(optarg, "CM108"))
					ptt_type = RIG_PTT_CM108;
				else if (!strcmp(optarg, "NONE"))
					ptt_type = RIG_PTT_NONE;
				else
					ptt_type = atoi(optarg);
				break;
			case 'd':
				tx_delay = 1000000 * atoi(optarg);
				break;
			case 't':
				tx_tail = 1000000 * atoi(optarg);
				tx_tail_ms = tx_tail / 1000000;
				break;
			case 'f':
				fullduplex = true;
				break;
			default:
				usage();
				return -1;
		}
	}
	
	eth_ar_call2mac(mac, call, ssid, false);
	
	rx_codec = codec2_create(mode);
	nr_samples = codec2_samples_per_frame(rx_codec);
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));
	tx_data = calloc(16, sizeof(uint8_t));

	mod_silence = calloc(nr_samples, sizeof(mod_silence[0]));

	fd_int = interface_init(netname, mac);
	sound_init(sounddev, cb_sound_in, nr_samples);
	hl_init();
	dtmf_init();

	prio();
	
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	sound_fdc_tx = sound_poll_count_tx();
	sound_fdc_rx = sound_poll_count_rx();
	nfds = sound_fdc_tx + sound_fdc_rx + 1;
	fds = calloc(sizeof(struct pollfd), nfds);
	
	sound_poll_fill_tx(fds, sound_fdc_tx);
	sound_poll_fill_rx(fds + sound_fdc_tx, sound_fdc_rx);
	poll_int = sound_fdc_tx + sound_fdc_rx;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;


	do {
		poll(fds, nfds, tx_state ? tx_tail_ms : -1);
		if (fds[poll_int].revents & POLLIN) {
			if (!tx_state && (!cdc || fullduplex)) {
				tx_delay_start();
			} else {
				interface_tx(cb_int_tx);
				tx_tail_extend();
			}
		}
		if (sound_poll_out_tx(fds, sound_fdc_tx)) {
			sound_out(mod_silence, nr_samples);
		}
		if (sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
		tx_tail_check();
	} while (1);
	
	
	return 0;
}
