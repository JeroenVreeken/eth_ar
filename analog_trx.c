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

#include <codec2/codec2.h>
#include <codec2/freedv_api.h>

#include "interface.h"
#include <eth_ar/eth_ar.h>
#include "sound.h"
#include "dtmf.h"
#include "alaw.h"
#include "io.h"
#include "beacon.h"
#include "ctcss.h"
#include "emphasis.h"
#include "freedv_eth_rx.h"

static bool verbose = false;
static bool cdc = false;
static bool fullduplex = false;

static ptt_type_t ptt_type = RIG_PTT_NONE;
static dcd_type_t dcd_type = RIG_DCD_NONE;
static int dcd_threshold = 1;

static struct beacon *beacon;

static struct CODEC2 *rx_codec;
static uint16_t rx_type;

static int nr_samples;
static int16_t *samples_rx;
static int nr_rx;

static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static struct ctcss *ctcss = NULL;
static struct emphasis *emphasis_p = NULL;
static struct emphasis *emphasis_d = NULL;
struct freedv *freedv;


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




static bool squelch(void)
{
	bool dcd;
	
	if (dcd_type == RIG_DCD_NONE)
		return io_state_rx_get();
	
	dcd = io_hl_dcd_get();

	return dcd;
}


static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl));
}


static void cb_sound_in(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r)
{
	bool rx_state = squelch();

	if (freedv)
		freedv_eth_rx(freedv, samples_r, nr_r);

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

	if (emphasis_d)
		emphasis_de(emphasis_d, samples_l, nr_l);
	dtmf_rx(samples_l, nr_l, cb_control);

	if (rx_codec) {
		while (nr_l) {
			int copy = nr_samples - nr_rx;
			if (copy > nr_l)
				copy = nr_l;

			memcpy(samples_rx + nr_rx, samples_l, copy * 2);
			samples_l += copy;
			nr_l -= copy;
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
		uint8_t alaw[nr_l];
		
		alaw_encode(alaw, samples_l, nr_l);
		
		interface_rx(bcast, mac, ETH_P_ALAW, alaw, nr_l);
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




static void dequeue_voice(void)
{
	struct tx_packet *p = queue_voice;
	if (!p) {
		if (!beacon) {
			sound_silence();
		} else {
			int16_t buffer[nr_samples];
			
			beacon_generate(beacon, buffer, nr_samples);
			if (emphasis_p)
				emphasis_pre(emphasis_p, p->samples, p->nr_samples);
			sound_out(buffer, nr_samples, true, true);
		}
	} else {
		queue_voice = p->next;
	
		if (beacon)
			beacon_generate_add(beacon, p->samples, p->nr_samples);
		if (ctcss)
			ctcss_add(ctcss, p->samples, p->nr_samples);
		if (emphasis_p)
			emphasis_pre(emphasis_p, p->samples, p->nr_samples);
		sound_out(p->samples, p->nr_samples, true, true);

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
				io_hl_ptt_set(true);
				tx_state_cnt = 0;
				if (ctcss)
					ctcss_reset(ctcss);
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
				io_hl_ptt_set(false);
				
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
	printf("-a\tUse A-Law encoding\n");
	printf("-B [sec]\tMorse beacon interval\n");
	printf("-b [msg]\tMorse beacon message\n");
	printf("-c [call]\town callsign\n");
	printf("-D [type]\tHAMlib DCD type\n");
	printf("-d [thrh]\tDCD threshold (default: %d)\n", dcd_threshold);
	printf("-e\tAdd pre-emphasis\n");
	printf("-f\tfull-duplex\n");
	printf("-F\tEnable FreeDV mode 2400B reception\n");
	printf("-I\tUse input device as toggle instead of keypress\n");
	printf("-i [dev]\tUse input device instead of DCD\n");
	printf("-M [mode]\tCodec2 mode\n");
	printf("-m [model]\tHAMlib rig model\n");
	printf("-n [dev]\tNetwork device name (default: \"analog\")\n");
	printf("-o\tAdd de-emphasis\n");
	printf("-P [type]\tHAMlib PTT type\n");
	printf("-p [dev]\tHAMlib PTT device file\n");
	printf("-r [rate]\tSound rate\n");
	printf("-S\tUse socket on existing network device\n");
	printf("-s [dev]\tSound device (default: \"default\")\n");
	printf("-T [freq]\tAdd CTCSS tone\n");
	printf("-t [msec]\tTX tail\n");
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
	int rate = 8000;
	int beacon_interval = 0;
	char *beacon_msg = NULL;
	int freedv_mode = FREEDV_MODE_2400B;
	rig_model_t rig_model;
	char *ptt_file = NULL;
	bool freedv_enabled = false;

	rig_model = 1; // set to dummy.
	
	while ((opt = getopt(argc, argv, "aB:b:c:d:D:efFIi:M:m:n:oP:p:r:Ss:t:T:v")) != -1) {
		switch(opt) {
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
			case 'd':
				dcd_threshold = atoi(optarg);
				break;
			case 'e':
				emphasis_p = emphasis_init();
				break;
			case 'f':
				fullduplex = true;
				break;
			case 'F':
				freedv_enabled = true;
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
				} else if (!strcmp(optarg, "700")) {
					mode = CODEC2_MODE_700;
				} else if (!strcmp(optarg, "700B")) {
					mode = CODEC2_MODE_700B;
				}
				break;
			case 'm':
				rig_model = atoi(optarg);
				break;
			case 'n':
				netname = optarg;
				break;
			case 'o':
				emphasis_d = emphasis_init();
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
			case 'p':
				ptt_file = optarg;
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
			case 'T': {
				double f = atof(optarg);
				double amp = 0.15;
				ctcss = ctcss_init(rate, f, amp);
				break;
			}
			case 't':
				tx_tail = atoi(optarg);
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

	int freedv_rate = 0;
	if (freedv_enabled) {
		freedv = freedv_open(freedv_mode);
		freedv_rate = freedv_get_modem_sample_rate(freedv);
		freedv_eth_rx_init(freedv, mac);
		freedv_set_callback_txt(freedv, freedv_eth_rx_vc_callback, NULL, NULL);
		freedv_set_callback_data(freedv, freedv_eth_rx_cb_datarx, NULL, NULL);
	}

	fd_int = interface_init(netname, mac, tap, 0);
	if (sound_init(sounddev, cb_sound_in, 
	    8000, 
	    8000, freedv_rate, 
	    rate)) {
		printf("Could not open sound device\n");
		return -1;
	}
	sound_set_nr(nr_samples);

	tx_tail /= 1000 / (8000 / nr_samples);

	io_hl_init(rig_model, dcd_threshold, ptt_type, ptt_file, dcd_type);
	dtmf_init();
	if (inputdev)
		io_init_input(inputdev, inputtoggle);
	io_init_tty();

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
			tx_state_machine();
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
