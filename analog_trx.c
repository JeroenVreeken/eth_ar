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
#include <eth_ar/eth_ar.h>
#include "sound.h"
#include "dtmf.h"
#include "alaw.h"
#include "input.h"
#include "beacon.h"


static bool verbose = false;
static bool cdc = false;
static bool fullduplex = false;
static bool tty_rx = false;

static RIG *rig;
static rig_model_t rig_model;
static char *ptt_file = NULL;
static ptt_type_t ptt_type = RIG_PTT_NONE;
static dcd_type_t dcd_type = RIG_DCD_NONE;
static int dcd_level = 0;
static int dcd_threshold = 1;

static struct beacon *beacon;

static struct CODEC2 *rx_codec;
static uint16_t rx_type;

static int nr_samples;
static int16_t *samples_rx;
static int nr_rx;

static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

enum tx_state {
	TX_STATE_OFF,
	TX_STATE_ON,
	TX_STATE_TAIL,
};

static enum tx_state tx_state = TX_STATE_OFF;

struct tx_packet {
	int16_t *samples;
	int nr_samples;
	
	struct tx_packet *next;
};

static struct tx_packet *queue_voice = NULL;



static bool squelch_input = false;

static bool squelch(void)
{
	dcd_t dcd;
	if (dcd_type == RIG_DCD_NONE)
		return squelch_input;
	
	rig_get_dcd(rig, RIG_VFO_CURR, &dcd);
	if (dcd == RIG_DCD_ON)
		dcd_level++;
	else
		dcd_level = 0;

	return dcd_level >= dcd_threshold;
}

static void input_cb(bool state)
{
	printf("DCD input: %d\n", state);
	squelch_input = state;
}


static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl));
}

static void handle_tty(void)
{
	ssize_t r;
	char buffer[2];
	
	r = read(0, buffer, 1);
	if (r == 1) {
		if (buffer[0] == '\n') {
			tty_rx = ! tty_rx;
		} else {
			buffer[1] = 0;
			cb_control(buffer);
		}
	}
}

static void cb_sound_in(int16_t *samples, int nr)
{
	bool rx_state = squelch() || tty_rx;

	if (!rx_state) {
		cdc = false;
		return;
	} else {
		if (fullduplex || tx_state == TX_STATE_OFF) {
			cdc = true;
		} else {
			return;
		}
	}

	dtmf_rx(samples, nr, cb_control);

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
				if (rx_state) {
					int bytes_per_codec_frame = (codec2_bits_per_frame(rx_codec) + 7)/8;
					unsigned char packed_codec_bits[bytes_per_codec_frame];
				
					codec2_encode(rx_codec, packed_codec_bits, samples_rx);

					interface_rx(bcast, mac, rx_type, packed_codec_bits, bytes_per_codec_frame);
				}
				nr_rx = 0;
			}
		}
	} else {
		uint8_t alaw[nr];
		
		alaw_encode(alaw, samples, nr);
		
		interface_rx(bcast, mac, ETH_P_ALAW, alaw, nr);
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


static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	int newmode = 0;
	bool is_c2 = true;

	if (tx_state == TX_STATE_OFF && (cdc && !fullduplex))
		return 0;
	
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
		case ETH_P_ALAW:
			is_c2 = false;
			break;
		default:
			return 0;
	}
	
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

	if (dcd_type != RIG_DCD_NONE)
		rig->state.dcdport.type.dcd = dcd_type;

	if (ptt_file)
		strncpy(rig->state.pttport.pathname, ptt_file, FILPATHLEN - 1);
	if (ptt_file)
		strncpy(rig->state.dcdport.pathname, ptt_file, FILPATHLEN - 1);

	retcode = rig_open(rig);
	if (retcode != RIG_OK) {
	  	fprintf(stderr,"rig_open: error = %s \n", rigerror(retcode));
		return -2;
	}

	rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);

	return 0;
}


static void dequeue_voice(void)
{
	struct tx_packet *p = queue_voice;
	if (!p) {
		if (!beacon) {
			sound_silence();
		} else {
			int16_t buffer[nr_samples];
			
			beacon_generate(beacon, buffer);
			sound_out(buffer, nr_samples);
		}
	} else {
		queue_voice = p->next;
	
		if (beacon)
			beacon_generate_add(beacon, p->samples, p->nr_samples);
		sound_out(p->samples, p->nr_samples);

		free(p->samples);
		free(p);
	}
}

static int tx_tail = 100;
static int tx_state_cnt;

static void tx_state_machine(void)
{
	tx_state_cnt++;
	bool bcn;
	if (beacon) {
		bcn = beacon_state_check(beacon);
		bcn = bcn && (!cdc || fullduplex);
	} else {
		bcn = false;
	}
	
	switch (tx_state) {
		case TX_STATE_OFF:
			if (queue_voice || bcn) {
				tx_state = TX_STATE_ON;
				rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_ON);
				tx_state_cnt = 0;
			} else {
				sound_silence();
				break;
			}
		case TX_STATE_ON:
			if (!queue_voice && !bcn) {
				tx_state = TX_STATE_TAIL;
				tx_state_cnt = 0;
			} else {
				dequeue_voice();
				break;
			}
		case TX_STATE_TAIL:
			if (tx_state_cnt >= tx_tail) {
				tx_state = TX_STATE_OFF;
				tx_state_cnt = 0;
				rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);
				
				/* make dcd insensitive for a little while */
				dcd_level = -dcd_threshold;
				
				sound_silence();
			} else {
				if (queue_voice || bcn) {
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					
					dequeue_voice();
				} else {
					sound_silence();
				}
			}
	}
}


static void usage(void)
{
	printf("Options:\n");
	printf("-v\tverbose\n");
	printf("-a\tUse A-Law encoding\n");
	printf("-B [sec]\tMorse beacon interval\n");
	printf("-b [msg]\tMorse beacon message\n");
	printf("-c [call]\town callsign\n");
	printf("-f\tfull-duplex\n");
	printf("-s [dev]\tSound device (default: \"default\")\n");
	printf("-n [dev]\tNetwork device name (default: \"freedv\")\n");
	printf("-S\tUse socket on existing network device\n");
	printf("-m [model]\tHAMlib rig model\n");
	printf("-P [type]\tHAMlib PTT type\n");
	printf("-D [type]\tHAMlib DCD type\n");
	printf("-p [dev]\tHAMlib PTT device file\n");
	printf("-d [thrh]\tDCD threshold (default: %d)\n", dcd_threshold);
	printf("-t [msec]\tTX tail\n");
	printf("-i [dev]\tUse input device instead of DCD\n");
	printf("-I\tUse input device as toggle instead of keypress\n");
	printf("-r [rate]\tSound rate\n");
	printf("-M [mode]\tCodec2 mode\n");
}

int main(int argc, char **argv)
{
	char *call = "pirate";
	char *sounddev = "default";
	char *netname = "analog";
	char *inputdev = NULL;
	bool inputtoggle = false;
	int fd_int;
	int fd_input = -1;
	struct pollfd *fds;
	int sound_fdc_tx;
	int sound_fdc_rx;
	int nfds;
	int poll_int;
	int poll_tty;
	int poll_input;
	int opt;
	int mode = CODEC2_MODE_3200;
	bool is_c2 = true;
	bool tap = true;
	int rate = 8000;
	int beacon_interval = 0;
	char *beacon_msg = NULL;
	
	rig_model = 1; // set to dummy.
	
	while ((opt = getopt(argc, argv, "vaB:b:c:d:i:Is:n:Sm:d:t:p:P:D:fr:M:")) != -1) {
		switch(opt) {
			case 'v':
				verbose = true;
				break;
			case 'a':
				is_c2 = false;
				break;
			case 'B':
				beacon_interval = atoi(optarg);
				break;
			case 'b':
				beacon_msg = optarg;
				break;
			case 'c':
				call = optarg;
				break;
			case 'd':
				dcd_threshold = atoi(optarg);
				break;
			case 'i':
				inputdev = optarg;
				break;
			case 'I':
				inputtoggle = true;
				break;
			case 's':
				sounddev = optarg;
				break;
			case 'n':
				netname = optarg;
				break;
			case 'S':
				tap = false;
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
				else if (!strcmp(optarg, "GPIO"))
					ptt_type = RIG_PTT_GPIO;
				else if (!strcmp(optarg, "GPION"))
					ptt_type = RIG_PTT_GPION;
				else if (!strcmp(optarg, "NONE"))
					ptt_type = RIG_PTT_NONE;
				else
					ptt_type = atoi(optarg);
				break;
			case 'D':
				if (!strcmp(optarg, "RIG"))
					dcd_type = RIG_DCD_RIG;
				else if (!strcmp(optarg, "DSR"))
					dcd_type = RIG_DCD_SERIAL_DSR;
				else if (!strcmp(optarg, "CTS"))
					dcd_type = RIG_DCD_SERIAL_CTS;
				else if (!strcmp(optarg, "CD"))
					dcd_type = RIG_DCD_SERIAL_CAR;
				else if (!strcmp(optarg, "PARALLEL"))
					dcd_type = RIG_DCD_PARALLEL;
				else if (!strcmp(optarg, "CM108"))
					dcd_type = RIG_DCD_CM108;
				else if (!strcmp(optarg, "NONE"))
					dcd_type = RIG_DCD_NONE;
				else
					dcd_type = atoi(optarg);
				break;
			case 't':
				tx_tail = atoi(optarg);
				break;
			case 'r':
				rate = atoi(optarg);
				break;
			case 'f':
				fullduplex = true;
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
				} else if (!strcmp(optarg, "700")) {
					mode = CODEC2_MODE_700;
				} else if (!strcmp(optarg, "700B")) {
					mode = CODEC2_MODE_700B;
				}
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
			case CODEC2_MODE_700:
				rx_type = ETH_P_CODEC2_700;
				break;
			case CODEC2_MODE_700B:
				rx_type = ETH_P_CODEC2_700B;
				break;
		}
	} else {
		rx_codec = NULL;
		nr_samples = 160;
	}
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));
	tx_data = calloc(16, sizeof(uint8_t));

	fd_int = interface_init(netname, mac, tap, 0);
	sound_init(sounddev, cb_sound_in, nr_samples, 8000, rate);

	tx_tail /= 1000 / (8000 / nr_samples);

	hl_init();
	dtmf_init();
	if (inputdev)
		fd_input = input_init(inputdev, inputtoggle);

	prio();
	
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	if (beacon_interval) {
		beacon = beacon_init(rate, nr_samples, beacon_interval, beacon_msg);
	}

	sound_fdc_tx = sound_poll_count_tx();
	sound_fdc_rx = sound_poll_count_rx();
	nfds = sound_fdc_tx + sound_fdc_rx + 1 + 1 + 1;
	fds = calloc(sizeof(struct pollfd), nfds);
	
	sound_poll_fill_tx(fds, sound_fdc_tx);
	sound_poll_fill_rx(fds + sound_fdc_tx, sound_fdc_rx);
	poll_int = sound_fdc_tx + sound_fdc_rx;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;
	poll_tty = poll_int + 1;
	fds[poll_tty].fd = 0;
	fds[poll_tty].events = POLLIN;
	poll_input = poll_tty + 1;
	fds[poll_input].fd = fd_input;
	fds[poll_input].events = POLLIN;


	do {
		poll(fds, nfds, -1);
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
		}
		if (fds[poll_tty].revents & POLLIN) {
			handle_tty();
		}
		if (fds[poll_input].revents & POLLIN) {
			input_handle(fd_input, input_cb);
		}
		if (sound_poll_out_tx(fds, sound_fdc_tx)) {
			tx_state_machine();
		}
		if (sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
	} while (1);
	
	
	return 0;
}
