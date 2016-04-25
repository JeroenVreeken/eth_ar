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

#define QUEUE_DATA_MAX 40

static bool verbose = false;
static bool cdc = false;
static bool fullduplex = false;

static struct freedv *freedv;
static struct CODEC2 *codec2;
static uint16_t eth_type_rx;
 
static int16_t *samples_rx;
static int nr_rx;

static int bytes_per_codec_frame;
static int bytes_per_eth_frame;

enum tx_state {
	TX_STATE_OFF,
	TX_STATE_DELAY,
	TX_STATE_ON,
	TX_STATE_TAIL,
};

static int tx_delay = 100;
static int tx_tail = 100;
static int tx_header = 500;
static int tx_header_max = 5000;
static enum tx_state tx_state = TX_STATE_OFF;
static int tx_state_cnt;
static int tx_state_data_header_cnt;

static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uint8_t rx_add[6];
static uint8_t tx_add[6];
static int rx_sync = 0;

#define RX_SYNC_THRESHOLD 20

static void cb_sound_in(int16_t *samples, int nr)
{
	if (tx_state != TX_STATE_OFF && !fullduplex)
		return;

	while (nr) {
		int nin = freedv_nin(freedv);
		int copy = nin - nr_rx;
		if (copy > nr)
			copy = nr;

		memcpy(samples_rx + nr_rx, samples, copy * sizeof(int16_t));
		samples += copy;
		nr -= copy;
		nr_rx += copy;
		
		if (nr_rx == nin) {
			unsigned char packed_codec_bits[bytes_per_codec_frame];
			
			bool old_cdc = cdc;
			cdc = false;
			
			int ret = freedv_codecrx(freedv, packed_codec_bits, samples_rx);

			/* Don't 'detect' a voide signal to soon. 
			   Might be nice to have some SNR number from the
			   2400B mode so we can take it into account */
			int sync = freedv_get_sync(freedv);
			if (!sync) {
				if (rx_sync)
					printf("RX sync lost\n");
				rx_sync = 0;
			} else {
				rx_sync++;
				if (ret)
					rx_sync++;
			}
			
			cdc = (rx_sync > RX_SYNC_THRESHOLD);
			if (ret && cdc) {
				int i;
				for (i = 0; i < bytes_per_codec_frame/bytes_per_eth_frame; i++) {
					interface_rx(
					    bcast, rx_add, eth_type_rx,
					    packed_codec_bits + i * bytes_per_eth_frame,
					    bytes_per_eth_frame);
				}
			}

			/* Reset rx address for voice to our own mac */
			if (!cdc && cdc != old_cdc) {
				printf("Reset RX add\n");
				memcpy(rx_add, mac, 6);
			}


			nr_rx = 0;
		}
	}
}


struct tx_packet {
	uint8_t from[6];
	uint8_t *data;
	size_t len;
	size_t off;
	
	struct tx_packet *next;
};

static struct tx_packet *queue_data = NULL;
static int queue_data_len = 0;
static struct tx_packet *queue_voice = NULL;
static struct tx_packet *queue_control = NULL;

static uint8_t *tx_data;
static size_t tx_data_len;

static void data_tx(void)
{
	int nr = freedv_get_n_nom_modem_samples(freedv);
	int16_t mod_out[nr];
	freedv_datatx(freedv, mod_out);
	
	if (nr > 0) {
		sound_out(mod_out, nr);
	}
}

static void check_tx_add(void)
{
	char callstr[9];
	int ssid;
	bool multicast;
	uint8_t *add;
	
	if (queue_voice)
		add = queue_voice->from;
	else
		add = mac;
	
	if (memcmp(add, tx_add, 6)) {
		memcpy(tx_add, add, 6);
		freedv_set_data_header(freedv, tx_add);

		eth_ar_mac2call(callstr, &ssid, &multicast, tx_add);
		printf("Voice TX add: %s-%d%s\n", callstr, ssid, multicast ? "" : "*");
	}
}

static void dequeue_voice(void)
{
	uint8_t *data = queue_voice->data;
	size_t len = queue_voice->len;
	
	check_tx_add();

	while (len) {
		size_t copy = len;
		if (copy + tx_data_len > bytes_per_codec_frame)
			copy = bytes_per_codec_frame - tx_data_len;
		
		memcpy(tx_data + tx_data_len, data, copy);
		tx_data_len += copy;
		data += copy;
		len -= copy;
		
		if (tx_data_len == bytes_per_codec_frame) {
			double energy = codec2_get_energy(codec2, tx_data);
//			printf("e: %f\n", energy);
			if (tx_state_data_header_cnt >= tx_header_max ||
			    ((queue_data || tx_state_data_header_cnt >= tx_header) && energy < 50.0) ||
			    energy < 1.0) {
				data_tx();
			} else {
				int nr = freedv_get_n_nom_modem_samples(freedv);
				int16_t mod_out[nr];
				freedv_codectx(freedv, mod_out, tx_data);
			
				if (nr > 0) {
					sound_out(mod_out, nr);
				}
			}
			tx_data_len = 0;
		}
	}
	
	struct tx_packet *old = queue_voice;
	queue_voice = old->next;
	
	free(old->data);
	free(old);
}

static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct tx_packet *packet, **queuep;
	
	packet = malloc(sizeof(struct tx_packet));
	if (!packet)
		return -1;
	packet->next = NULL;

	if (eth_type == eth_type_rx) {
		packet->data = malloc(len);
		if (!packet->data)
			goto err_packet;

		packet->len = len;
		memcpy(packet->data, data, len);
		memcpy(packet->from, from, 6);
		
		for (queuep = &queue_voice; *queuep; queuep = &(*queuep)->next);
		*queuep = packet;
	} else if (eth_type == ETH_P_AR_CONTROL) {
		packet->data = malloc(len);
		if (!packet->data)
			goto err_packet;

		memcpy(packet->data, data, len);
		packet->len = len;
		packet->off = 0;
		
		for (queuep = &queue_control; *queuep; queuep = &(*queuep)->next);
		*queuep = packet;
	} else {
		if (queue_data_len >= QUEUE_DATA_MAX)
			goto err_packet;
			
		packet->data = malloc(len + 6 + 6 + 2);
		if (!packet->data)
			goto err_packet;

		packet->len = len + 6 + 6 + 2;
		memcpy(packet->data + 0, to, 6);
		memcpy(packet->data + 6, from, 6);
		packet->data[12] = eth_type >> 8;
		packet->data[13] = eth_type & 0xff;
		memcpy(packet->data + 14, data, len);

		for (queuep = &queue_data; *queuep; queuep = &(*queuep)->next);
		
		queue_data_len++;
		*queuep = packet;
	}

	return 0;

err_packet:
	free(packet);
	return -1;
}

static void freedv_cb_datarx(void *arg, unsigned char *packet, size_t size)
{
	if (size == 12) {
		if (memcmp(rx_add, packet + 6, 6)) {
			char callstr[9];
			int ssid;
			bool multicast;
		
			memcpy(rx_add, packet + 6, 6);

			eth_ar_mac2call(callstr, &ssid, &multicast, rx_add);
			printf("Voice RX add: %s-%d%s\n", callstr, ssid, multicast ? "" : "*");
		}
	} else if (size > 14) {
		uint16_t type = (packet[12] << 8) | packet[13];
		interface_rx(packet, packet+6, type, packet + 14, size - 14);
	}
}

static void freedv_cb_datatx(void *arg, unsigned char *packet, size_t *size)
{
	if (tx_state == TX_STATE_ON) {
		if (!queue_data || tx_state_data_header_cnt >= tx_header) {
			tx_state_data_header_cnt = 0;
			*size = 0;
		} else {
			struct tx_packet *qp = queue_data;
		
			queue_data = qp->next;
		
			memcpy(packet, qp->data, qp->len);
			*size = qp->len;
			queue_data_len--;
		
			free(qp->data);
			free(qp);
		}
	} else {
		/* TX not on, just send header frames as filler */
		*size = 0;
	}
}

static int prio(void)
{
	struct sched_param param;

	param.sched_priority = 90;

	if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		printf("sched_setscheduler() failed: %s\n",
		    strerror(errno));
	}

	if (mlockall(MCL_CURRENT | MCL_FUTURE )) {
		printf("mlockall failed: %s\n",
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


void tx_state_machine(void)
{
	tx_state_cnt++;
	switch (tx_state) {
		case TX_STATE_OFF:
			if ((queue_voice || queue_data) && (!cdc || fullduplex)) {
//				printf("OFF -> DELAY\n");
				tx_state = TX_STATE_DELAY;
				tx_state_cnt = 0;
				rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_ON);

				check_tx_add();
			} else {
				sound_silence();
				break;
			}
		case TX_STATE_DELAY:
//			printf("%d %d\n", tx_state_cnt, tx_delay);
			if (tx_state_cnt >= tx_delay) {
//				printf("DELAY -> ON\n");
				tx_state = TX_STATE_ON;
				tx_state_cnt = 0;
				tx_state_data_header_cnt = 0;
			}
			if (queue_voice) {
				dequeue_voice();
			} else {
				data_tx();
			}
			break;
		case TX_STATE_ON:
			if (!queue_voice && ! queue_data && !freedv_data_ntxframes(freedv)) {
//				printf("ON -> TAIL\n");
				tx_state = TX_STATE_TAIL;
				tx_state_cnt = 0;
			} else {
				tx_state_data_header_cnt++;
				if (queue_voice) {
					dequeue_voice();
				} else {
					data_tx();
				}
				break;
			}
		case TX_STATE_TAIL:
			if (tx_state_cnt >= tx_tail) {
//				printf("TAIL -> OFF\n");
				tx_state = TX_STATE_OFF;
				tx_state_cnt = 0;
				rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);
			} else {
				if (queue_voice || queue_data) {
//					printf("TAIL -> ON\n");
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					tx_state_data_header_cnt = 0;
					
					check_tx_add();
				}
				if (queue_voice) {
					dequeue_voice();
				} else {
					data_tx();
				}
				break;
			}
	}
}


static void vc_callback_rx(void *arg, char c)
{
	uint8_t msg[2];
	
	printf("VC RX: 0x%x %c\n", c, c);
	msg[0] = c;
	msg[1] = 0;
	interface_rx(bcast, rx_add, ETH_P_AR_CONTROL, msg, 1);
}

static char vc_callback_tx(void *arg)
{
	char c;
	
	if (queue_control && tx_state == TX_STATE_ON) {
		struct tx_packet *qp = queue_control;
		
		c = qp->data[qp->off++];
		if (c < 0)
			c = 0;
		if (qp->off >= qp->len) {
			queue_control = qp->next;
		
			free(qp->data);
			free(qp);
		}
		printf("VC TX: 0x%x %c\n", c, c);
	} else {
		c = 0;
	}
	
	return c;
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
					codec2 = codec2_create(CODEC2_MODE_1300);
				} else if (!strcmp(optarg, "700")) {
					mode = FREEDV_MODE_700;
					type = ETH_P_CODEC2_700;
					codec2 = codec2_create(CODEC2_MODE_700);
				} else if (!strcmp(optarg, "700B")) {
					mode = FREEDV_MODE_700B;
					type = ETH_P_CODEC2_700B;
					codec2 = codec2_create(CODEC2_MODE_700B);
				} else if (!strcmp(optarg, "2400A")) {
					mode = FREEDV_MODE_2400A;
					type = ETH_P_CODEC2_1300;
					codec2 = codec2_create(CODEC2_MODE_1300);
				} else if (!strcmp(optarg, "2400B")) {
					mode = FREEDV_MODE_2400B;
					type = ETH_P_CODEC2_1300;
					codec2 = codec2_create(CODEC2_MODE_1300);
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
				tx_delay = atoi(optarg);
				break;
			case 't':
				tx_tail = atoi(optarg);
				break;
			case 'f':
				fullduplex = true;
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
	memcpy(rx_add, mac, 6);
	memcpy(tx_add, mac, 6);
	
	freedv = freedv_open(mode);	

	freedv_set_callback_txt(freedv, vc_callback_rx, vc_callback_tx, NULL);
	freedv_set_callback_data(freedv, freedv_cb_datarx, freedv_cb_datatx, NULL);
	freedv_set_data_header(freedv, mac);

	nr_samples = freedv_get_n_max_modem_samples(freedv);
	printf("max number of modem samples: %d\n", nr_samples);
        bytes_per_eth_frame = codec2_bits_per_frame(freedv_get_codec2(freedv));
	bytes_per_eth_frame += 7;
	bytes_per_eth_frame /= 8;
	printf("bytes per ethernet frame: %d\n", bytes_per_eth_frame);
	int rat = freedv_get_n_codec_bits(freedv) / codec2_bits_per_frame(freedv_get_codec2(freedv));
	printf("ehternet frames per freedv frame: %d\n", rat);
	bytes_per_codec_frame = bytes_per_eth_frame * rat;
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));
	tx_data = calloc(bytes_per_codec_frame, sizeof(uint8_t));

	eth_type_rx = type;
	fd_int = interface_init(netname, mac, true);
	int rate = freedv_get_modem_sample_rate(freedv);
	printf("sample rate: %d\n", rate);
	sound_init(sounddev, cb_sound_in, nr_samples, rate, rate);

	int period_msec = 1000 / (rate / nr_samples);
	printf("TX period: %d msec\n", period_msec);

	tx_tail /= period_msec;
	tx_delay /= period_msec;
	tx_header /= period_msec;
	tx_header_max /= period_msec;
	
	printf("TX delay: %d periods\n", tx_delay);
	printf("TX tail: %d periods\n", tx_tail);
	printf("TX header: %d periods\n", tx_header);
	printf("TX header max: %d periods\n", tx_header_max);

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
		poll(fds, nfds, -1);
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
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
