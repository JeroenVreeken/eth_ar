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
#include <sys/mman.h>
#include <hamlib/rig.h>

#include <codec2/freedv_api.h>
#include <codec2/codec2.h>

#include "interface.h"
#include "eth_ar.h"
#include "sound.h"

static bool verbose = false;
static bool cdc = false;
static bool fullduplex = false;

static struct freedv *freedv;

int16_t *samples_rx;
int nr_rx;

int bytes_per_codec_frame;
int bytes_per_eth_frame;

static void cb_sound_in(int16_t *samples, int nr)
{
	while (nr) {
		int nin = freedv_nin(freedv);
		int copy = nin - nr_rx;
		if (copy > nr)
			copy = nr;

		memcpy(samples_rx + nr_rx, samples, copy);
		samples += copy;
		nr -= copy;
		nr_rx += copy;
		
		if (nr_rx == nin) {
			unsigned char packed_codec_bits[bytes_per_codec_frame];
			int ret = freedv_codecrx(freedv, packed_codec_bits, samples_rx);
			if (ret) {
				int i;
				for (i = 0; i < bytes_per_codec_frame/bytes_per_eth_frame; i++) {
					interface_rx(
					    packed_codec_bits + i * bytes_per_eth_frame,
					    bytes_per_eth_frame);
				}
			}
			nr_rx = 0;
		}
	}
}

uint8_t *tx_data;
size_t tx_data_len;

static int cb_int_tx(uint8_t *data, size_t len)
{
	while (len) {
		size_t copy = len;
		if (copy + tx_data_len > bytes_per_codec_frame)
			copy = bytes_per_codec_frame - tx_data_len;
		
		memcpy(tx_data + tx_data_len, data, copy);
		tx_data_len += copy;
		data += copy;
		len -= copy;
		
		if (tx_data_len == bytes_per_codec_frame) {
			int nr = freedv_get_n_nom_modem_samples(freedv);
			int16_t mod_out[nr];
			freedv_codectx(freedv, mod_out, tx_data);
			
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
	printf("-M\tfreedv mode\n");
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
	char *netname = "freedv";
	int ssid = 0;
	uint8_t mac[6];
	int fd_int;
	struct pollfd *fds;
	int sound_fdc_tx;
	int sound_fdc_rx;
	int nfds;
	int poll_int;
	int opt;
	uint16_t type = ETH_P_CODEC2_700;
	int nr_samples;
	int mode = FREEDV_MODE_700;
	
	rig_model = 1; // set to dummy.
	
	while ((opt = getopt(argc, argv, "M:vc:s:n:m:d:t:p:P:f")) != -1) {
		switch(opt) {
			case 'M':
				if (!strcmp(optarg, "1600")) {
					mode = FREEDV_MODE_1600;
					type = ETH_P_CODEC2_1300;
				} else if (!strcmp(optarg, "700")) {
					mode = FREEDV_MODE_700;
					type = ETH_P_CODEC2_700;
				} else if (!strcmp(optarg, "700B")) {
					mode = FREEDV_MODE_700B;
					type = ETH_P_CODEC2_700B;
				} else if (!strcmp(optarg, "2400A")) {
					mode = FREEDV_MODE_2400A;
					type = ETH_P_CODEC2_1300;
				} else if (!strcmp(optarg, "2400B")) {
					mode = FREEDV_MODE_2400B;
					type = ETH_P_CODEC2_1300;
				}
				break;
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
	
	freedv = freedv_open(mode);	

	nr_samples = freedv_get_n_max_modem_samples(freedv);
        bytes_per_eth_frame = codec2_bits_per_frame(freedv_get_codec2(freedv));
	bytes_per_eth_frame += 7;
	bytes_per_eth_frame /= 8;
	printf("bytes per ethernet frame: %d\n", bytes_per_eth_frame);
	int rat = freedv_get_n_codec_bits(freedv) / codec2_bits_per_frame(freedv_get_codec2(freedv));
	printf("ehternet frames per freedv frame: %d\n", rat);
	bytes_per_codec_frame = bytes_per_eth_frame * rat;
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));
	tx_data = calloc(bytes_per_codec_frame, sizeof(uint8_t));

	fd_int = interface_init(netname, mac, type);
	int rate = freedv_get_modem_sample_rate(freedv);
	printf("sample rate: %d\n", rate);
	sound_init(sounddev, cb_sound_in, nr_samples, rate);
	hl_init();

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
//TODO send silence	packet_tx_info(&packet);
		}
		if (sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
		tx_tail_check();
	} while (1);
	
	
	return 0;
}
