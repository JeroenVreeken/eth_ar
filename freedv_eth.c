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
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <hamlib/rig.h>
#include <math.h>

#include <codec2/freedv_api.h>

#include "interface.h"
#include "eth_ar/eth_ar.h"
#include "eth_ar_codec2.h"
#include "sound.h"
#include "eth_ar/fprs.h"
#include "nmea.h"
#include "freedv_eth_rx.h"
#include "freedv_eth.h"
#include "freedv_eth_config.h"
#include "io.h"
#include "eth_ar/alaw.h"

static bool verbose;
static bool fullduplex;
static bool repeater;
static bool vc_control;

static struct freedv *freedv;
static int freedv_rx_channel;
static int freedv_tx_channel;
static int tx_codecmode;

static int analog_rx_channel;
static bool baseband_out;
static bool baseband_in;
static bool baseband_in_tx;

static int tx_delay_msec;
static int tx_tail_msec;
static bool freedv_hasdata;
static uint8_t mac[ETH_AR_MAC_SIZE];

static struct freedv_eth_transcode *tc = NULL;
static struct freedv_eth_transcode *tc_iface = NULL;

static struct nmea_state *nmea;

enum tx_mode {
	TX_MODE_NONE,
	TX_MODE_FREEDV,
	TX_MODE_ANALOG,
	TX_MODE_MIXED,
};

static enum tx_mode tx_mode;

enum rx_mode {
	RX_MODE_NONE,
	RX_MODE_FREEDV,
	RX_MODE_ANALOG,
	RX_MODE_MIXED,
};

static enum rx_mode rx_mode;

void freedv_eth_voice_rx(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, bool local_rx,
    uint8_t transmission, double level_dbm)
{
	struct tx_packet *packet;
	uint8_t level = eth_ar_dbm_encode(level_dbm);

	if (tx_mode != TX_MODE_NONE) {
		if (repeater || (baseband_in_tx && !local_rx)) {
			if (len > tx_packet_max())
				return;
			packet = tx_packet_alloc();
			packet->len = len;
			memcpy(packet->data, data, len);
			memcpy(packet->from, from, 6);
		
			freedv_eth_transcode(tc, packet, tx_codecmode, eth_type);

			packet->local_rx = local_rx;
			enqueue_voice(packet, 0, -10);
		}
		if (local_rx && baseband_out) {
			if (len > tx_packet_max())
				return;
			packet = tx_packet_alloc();
			packet->len = len;
			memcpy(packet->data, data, len);
			memcpy(packet->from, from, 6);
		
			freedv_eth_transcode(tc, packet, CODEC_MODE_NATIVE16, eth_type);

			packet->local_rx = local_rx;
			enqueue_baseband(packet);
		}
	}

	if (eth_type == ETH_P_NATIVE16) {
		packet = tx_packet_alloc();
		packet->len = len;
		memcpy(packet->data, data, len);
		memcpy(packet->from, from, 6);
		
		freedv_eth_transcode(tc_iface, packet, CODEC_MODE_ALAW, eth_type);

		interface_rx(to, from, ETH_P_ALAW, packet->data, packet->len, transmission, level);
		tx_packet_free(packet);
	} else {
		interface_rx(to, from, eth_type, data, len, transmission, level);
	}
}

static void cb_sound_in(int16_t *samples_l, int16_t *samples_r, int nr_l, int nr_r)
{
	if ((freedv_eth_tx_ptt() || freedv_eth_txa_ptt()) && !fullduplex)
		return;

	if (rx_mode == RX_MODE_FREEDV ||
	    rx_mode == RX_MODE_MIXED) {
		if (freedv_rx_channel == 0) {
			freedv_eth_rx(samples_l, nr_l);
		} else {
			freedv_eth_rx(samples_r, nr_r);
		}
	}
	if (rx_mode == RX_MODE_ANALOG ||
	    rx_mode == RX_MODE_MIXED) {
		if (analog_rx_channel == 0) {
			freedv_eth_rxa(samples_l, nr_l);
		} else {
			freedv_eth_rxa(samples_r, nr_r);
		}
	}
}


static void freedv_eth_tx_none(int nr)
{
	sound_out_lr(NULL, NULL, nr);
}


static int cb_int_tx(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct tx_packet *packet;
	
	if (tx_mode == TX_MODE_NONE)
		return 0;
	
	if (eth_ar_eth_p_isvoice(eth_type)) {
		if (len > tx_packet_max() || len < 2)
			return 0;
		uint8_t transmission = data[0];
		uint8_t level = data[1];
		packet = tx_packet_alloc();
		packet->len = len-2;
		memcpy(packet->data, data+2, len-2);
		memcpy(packet->from, from, 6);
		
		freedv_eth_transcode(tc, packet, tx_codecmode, eth_type);

		int q = enqueue_voice(packet, transmission, eth_ar_dbm_decode(level));

		if (q && baseband_out) {
			packet = tx_packet_alloc();
			packet->len = len-2;
			memcpy(packet->data, data+2, len-2);
			memcpy(packet->from, from, 6);
		
			freedv_eth_transcode(tc, packet, CODEC_MODE_NATIVE16, eth_type);

			enqueue_baseband(packet);
		}
	} else {
		if (eth_type == ETH_P_FPRS && !memcmp(mac, from, 6)) {
			struct fprs_frame *frame = fprs_frame_create();
			fprs_frame_data_set(frame, data, len);
			
			struct fprs_element *el = NULL;
			el = fprs_frame_element_by_type(frame, FPRS_DMLASSOC);
			if (el) {
				bool assoc = false;
				if (fprs_element_size(el)) {
					assoc = true;
				}
				io_dmlassoc_set(assoc);
			}
			fprs_frame_destroy(frame);
		}
		if (tx_mode == TX_MODE_FREEDV || tx_mode == TX_MODE_MIXED) {
//			printf("Data: %d %x\n", eth_type, eth_type);
			/* TODO: send control as DTMF in analog mode */
			if (eth_type == ETH_P_AR_CONTROL && vc_control) {
				packet = tx_packet_alloc();
				memcpy(packet->data, data + 2, len - 2);
				packet->len = len -2;
				packet->off = 0;
			
				enqueue_control(packet);
			} else if (freedv_hasdata) {
				packet = tx_packet_alloc();
				struct ether_header *header = (void*)packet->data;
				packet->len = len + sizeof(struct ether_header);
				memcpy(header->ether_dhost, to, 6);
				memcpy(header->ether_shost, from, 6);
				header->ether_type = htons(eth_type);
				memcpy(packet->data + sizeof(struct ether_header), data, len);

				enqueue_data(packet);
			}
		}
	}

	return 0;
}



static int prio(void)
{
	struct sched_param param;

	param.sched_priority = sched_get_priority_max(SCHED_FIFO);

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





void read_nmea(int fd_nmea)
{
	char buffer[256];
	ssize_t r;
	
	r = read(fd_nmea, buffer, 256);
	if (r > 0) {
		bool old_valid = nmea->position_valid;
	
		nmea_parse(nmea, buffer, r);
		
		if (old_valid != nmea->position_valid) {
			printf("GPS status changed: %s\n",
			    nmea->position_valid ? "valid" : "invalid");
		}
	}
}

bool freedv_eth_cdc(void)
{
	return freedv_eth_rx_cdc() || freedv_eth_rxa_cdc();
}

static void usage(void)
{
	printf("freedv_eth <config file>\n");
}

int main(int argc, char **argv)
{
	int fd_int;
	int fd_nmea = -1;
	int fd_modem = -1;
	struct pollfd *fds;
	int sound_fdc_tx = 0;
	int sound_fdc_rx = 0;
	int nfds;
	int poll_i = 0;
	int poll_int = 0;
	int poll_nmea = 0;
	int poll_modem = 0;
	uint16_t type;
	int nr_samples = 0;
	int freedv_mode = -1;
	rig_model_t rig_model;
	ptt_type_t ptt_type = RIG_PTT_NONE;
	dcd_type_t dcd_type = RIG_DCD_NONE;
	bool analog_in = false;

	bool need_sound = false;

	if (argc < 2) {
		usage();
		return -1;
	}
	
	if (freedv_eth_config_load(argv[1])) {
		printf("Failed to load config file %s\n", argv[1]);
		return -1;
	}
	
	char *nmeadev = freedv_eth_config_value("nmea_device", NULL, NULL);
	char *sounddev = freedv_eth_config_value("sound_device", NULL, "default");
	int sound_rate = atoi(freedv_eth_config_value("sound_rate", NULL, "48000"));
	char *netname = freedv_eth_config_value("network_device", NULL, "freedv");
	char *call = freedv_eth_config_value("callsign", NULL, "pirate");
	tx_delay_msec = atoi(freedv_eth_config_value("tx_delay", NULL, "100"));
	tx_tail_msec = atoi(freedv_eth_config_value("tx_tail", NULL, "100"));
	fullduplex = atoi(freedv_eth_config_value("fullduplex", NULL, "0"));
	repeater = atoi(freedv_eth_config_value("repeater", NULL, "0"));
	verbose = atoi(freedv_eth_config_value("verbose", NULL, "0"));
	char *freedv_mode_str = freedv_eth_config_value("freedv_mode", NULL, "1600");
	rig_model = atoi(freedv_eth_config_value("rig_model", NULL, "1"));
	char *rig_file = freedv_eth_config_value("rig_file", NULL, NULL);
	char *ptt_file = freedv_eth_config_value("rig_ptt_file", NULL, NULL);
	char *dcd_file = freedv_eth_config_value("rig_dcd_file", NULL, NULL);
	vc_control = atoi(freedv_eth_config_value("control_vc", NULL, "0"));
	char *rig_ptt_type = freedv_eth_config_value("rig_ptt_type", NULL, "NONE");
	char *rig_dcd_type = freedv_eth_config_value("rig_dcd_type", NULL, "NONE");
	char *freedv_tx_sound_channel = freedv_eth_config_value("freedv_tx_sound_channel", NULL, "left");
	char *freedv_rx_sound_channel = freedv_eth_config_value("freedv_rx_sound_channel", NULL, "left");
	char *analog_rx_sound_channel = freedv_eth_config_value("analog_rx_sound_channel", NULL, "left");
	char *tx_mode_str = freedv_eth_config_value("tx_mode", NULL, "freedv");
	char *rx_mode_str = freedv_eth_config_value("rx_mode", NULL, "freedv");
	int dcd_threshold = atoi(freedv_eth_config_value("analog_rx_dcd_threshold", NULL, "1"));
	baseband_out = atoi(freedv_eth_config_value("baseband_out", NULL, "0"));
	baseband_in = atoi(freedv_eth_config_value("baseband_in", NULL, "0"));
	baseband_in_tx = atoi(freedv_eth_config_value("baseband_in_tx", NULL, "0"));
	char *modem_file = freedv_eth_config_value("external_modem", NULL, NULL);

	if (!modem_file) {
		need_sound = true;
	}
	
	if (!strcmp(freedv_mode_str, "1600")) {
		freedv_mode = FREEDV_MODE_1600;
		freedv_hasdata = false;
#if defined(FREEDV_MODE_700)
	} else if (!strcmp(freedv_mode_str, "700")) {
		freedv_mode = FREEDV_MODE_700;
		freedv_hasdata = false;
#endif
#if defined(FREEDV_MODE_700B)
	} else if (!strcmp(freedv_mode_str, "700B")) {
		freedv_mode = FREEDV_MODE_700B;
		freedv_hasdata = false;
#endif
	} else if (!strcmp(freedv_mode_str, "700C")) {
		freedv_mode = FREEDV_MODE_700C;
	} else if (!strcmp(freedv_mode_str, "700D")) {
		freedv_mode = FREEDV_MODE_700D;
		freedv_hasdata = false;
	} else if (!strcmp(freedv_mode_str, "2400A")) {
		freedv_mode = FREEDV_MODE_2400A;
		freedv_hasdata = true;
	} else if (!strcmp(freedv_mode_str, "2400B")) {
		freedv_mode = FREEDV_MODE_2400B;
		freedv_hasdata = true;
	} else if (!strcmp(freedv_mode_str, "800XA")) {
		freedv_mode = FREEDV_MODE_800XA;
		freedv_hasdata = true;
#if defined(FREEDV_MODE_2020)
	} else if (!strcmp(freedv_mode_str, "2020")) {
		freedv_mode = FREEDV_MODE_2020;
		freedv_hasdata = false;
#endif
#if defined(FREEDV_MODE_6000)
	} else if (!strcmp(freedv_mode_str, "6000")) {
		freedv_mode = FREEDV_MODE_6000;
		freedv_hasdata = true;
#endif
	} else {
		printf("Invalid FreeDV mode\n");
		return -1;
	}
	
	printf("TX mode: %s\n", tx_mode_str);
	if (!strcmp(tx_mode_str, "none")) {
		tx_mode = TX_MODE_NONE;
	} else if (!strcmp(tx_mode_str, "freedv")) {
		tx_mode = TX_MODE_FREEDV;
	} else if (!strcmp(tx_mode_str, "analog")) {
		tx_mode = TX_MODE_ANALOG;
		need_sound = true;
		analog_in = true;
	} else if (!strcmp(rx_mode_str, "mixed")) {
		tx_mode = RX_MODE_MIXED;
		need_sound = true;
		analog_in = true;
	} else {
		printf("Invalid tx_mode\n");
		return -1;
	}
	
	printf("RX mode: %s\n", rx_mode_str);
	if (!strcmp(rx_mode_str, "none")) {
		rx_mode = RX_MODE_NONE;
	} else if (!strcmp(rx_mode_str, "freedv")) {
		rx_mode = RX_MODE_FREEDV;
	} else if (!strcmp(rx_mode_str, "analog")) {
		rx_mode = RX_MODE_ANALOG;
		need_sound = true;
	} else if (!strcmp(rx_mode_str, "mixed")) {
		rx_mode = RX_MODE_MIXED;
		need_sound = true;
	} else {
		printf("Invalid rx_mode\n");
		return -1;
	}
	
	if (!strcmp(freedv_tx_sound_channel, "left")) {
		freedv_tx_channel = 0;
	} else if (!strcmp(freedv_tx_sound_channel, "right")) {
		freedv_tx_channel = 1;
	} else {
		/* Assume it is a number and limit it to odd or even */
		freedv_tx_channel = atoi(freedv_tx_sound_channel) & 0x1;
	}
	if (!strcmp(freedv_rx_sound_channel, "left")) {
		freedv_rx_channel = 0;
	} else if (!strcmp(freedv_rx_sound_channel, "right")) {
		freedv_rx_channel = 1;
	} else {
		/* Assume it is a number and limit it to odd or even */
		freedv_rx_channel = atoi(freedv_rx_sound_channel) & 0x1;
	}

	if (!strcmp(analog_rx_sound_channel, "left")) {
		analog_rx_channel = 0;
	} else if (!strcmp(analog_rx_sound_channel, "right")) {
		analog_rx_channel = 1;
	} else {
		/* Assume it is a number and limit it to odd or even */
		analog_rx_channel = atoi(analog_rx_sound_channel) & 0x1;
	}
	
	if (!strcmp(rig_ptt_type, "RIG"))
		ptt_type = RIG_PTT_RIG;
	else if (!strcmp(rig_ptt_type, "RIGMICDATA"))
		ptt_type = RIG_PTT_RIG_MICDATA;
	else if (!strcmp(rig_ptt_type, "DTR"))
		ptt_type = RIG_PTT_SERIAL_DTR;
	else if (!strcmp(rig_ptt_type, "RTS"))
		ptt_type = RIG_PTT_SERIAL_RTS;
	else if (!strcmp(rig_ptt_type, "PARALLEL"))
		ptt_type = RIG_PTT_PARALLEL;
	else if (!strcmp(rig_ptt_type, "CM108"))
		ptt_type = RIG_PTT_CM108;
	else if (!strcmp(rig_ptt_type, "GPIO"))
		ptt_type = RIG_PTT_GPIO;
	else if (!strcmp(rig_ptt_type, "GPION"))
		ptt_type = RIG_PTT_GPION;
	else if (!strcmp(rig_ptt_type, "NONE"))
		ptt_type = RIG_PTT_NONE;
	else
		ptt_type = atoi(rig_ptt_type);

	if (!strcmp(rig_dcd_type, "RIG"))
		dcd_type = RIG_DCD_RIG;
	else if (!strcmp(rig_dcd_type, "DSR"))
		dcd_type = RIG_DCD_SERIAL_DSR;
	else if (!strcmp(rig_dcd_type, "CTS"))
		dcd_type = RIG_DCD_SERIAL_CTS;
	else if (!strcmp(rig_dcd_type, "CD"))
		dcd_type = RIG_DCD_SERIAL_CAR;
	else if (!strcmp(rig_dcd_type, "PARALLEL"))
		dcd_type = RIG_DCD_PARALLEL;
	else if (!strcmp(rig_dcd_type, "CM108"))
		dcd_type = RIG_DCD_CM108;
	else if (!strcmp(rig_dcd_type, "NONE"))
		dcd_type = RIG_DCD_NONE;
	else if (!strcmp(rig_dcd_type, "GPIO"))
		dcd_type = RIG_DCD_GPIO;
	else if (!strcmp(rig_dcd_type, "GPION"))
		dcd_type = RIG_DCD_GPION;
	else
		dcd_type = atoi(rig_dcd_type);

	io_hl_init(rig_model, dcd_threshold, ptt_type, ptt_file, dcd_type, dcd_file, rig_file);
	
	type = freedv_eth_mode2type(freedv_mode);
	
	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		return -1;
	}
	
	printf("Opening freedv: mode %s: %d\n", freedv_mode_str, freedv_mode);
	freedv = freedv_open(freedv_mode);

	freedv_set_callback_txt(freedv, freedv_eth_rx_vc_callback, freedv_eth_tx_vc_callback, NULL);
	freedv_set_callback_data(freedv, freedv_eth_rx_cb_datarx, freedv_eth_tx_cb_datatx, NULL);
	freedv_set_data_header(freedv, mac);

	if (tx_mode == TX_MODE_FREEDV) {
		tx_codecmode = eth_ar_eth_p_codecmode(type);
	} else {
		/* Decode to speech shorts, but don't recode... */
		tx_codecmode = CODEC_MODE_NATIVE16;
	}
	int force_channels_in = 0;
	if (rx_mode == RX_MODE_MIXED)
		force_channels_in = 2;

	fd_int = interface_init(netname, mac, true, 0);
	if (need_sound) {
		sound_rate = sound_init(sounddev, cb_sound_in, sound_rate, force_channels_in, 2);
		if (sound_rate < 0)
			return -1;
	}
	
	tc = freedv_eth_transcode_init(sound_rate);
	tc_iface = freedv_eth_transcode_init(sound_rate);
	
	if (need_sound) {
		if (tx_mode == TX_MODE_FREEDV) {
			int freedv_rate = freedv_get_modem_sample_rate(freedv);
			printf("freedv sample rate: %d\n", freedv_rate);
			nr_samples = freedv_get_n_nom_modem_samples(freedv) * sound_rate / freedv_rate;
		} else {
			nr_samples = FREEDV_ALAW_NR_SAMPLES * sound_rate / FREEDV_ALAW_RATE;
		}
		printf("nom number of modem samples: %d\n", nr_samples);

		sound_set_nr(nr_samples);
	}
	
	freedv_eth_rx_init(freedv, mac, sound_rate);
	if (analog_in)
		freedv_eth_rxa_init(sound_rate, mac, nr_samples);

	if (baseband_in)
		freedv_eth_bb_in_init(sound_rate, mac, nr_samples);


	if (tx_mode == TX_MODE_FREEDV || tx_mode == TX_MODE_MIXED) {
		freedv_eth_tx_init(freedv, mac, nmea, fullduplex,
		    sound_rate,
		    tx_tail_msec, tx_delay_msec,
		    freedv_tx_channel,
		    modem_file ? true : false);
	}
	if (tx_mode == TX_MODE_ANALOG || tx_mode == TX_MODE_MIXED) {
		freedv_eth_txa_init(fullduplex, 
		    sound_rate, 
		    tx_tail_msec);
	}
	
	if (nmeadev) {
		fd_nmea = open(nmeadev, O_RDONLY);
		if (fd_nmea >= 0) {
			nmea = nmea_state_create();
			printf("GPS device: %s\n", nmeadev);
		}
	}
	if (modem_file) {
		fd_modem = freedv_eth_modem_init(modem_file, freedv);
		if (fd_modem < 0) {
			printf("Could not open modem: %s\n", modem_file);
			return -1;
		}
	}


	prio();
	
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	if (need_sound) {
		sound_fdc_tx = sound_poll_count_tx();
		sound_fdc_rx = sound_poll_count_rx();
	}
	nfds = sound_fdc_tx + sound_fdc_rx + 1 + (nmea ? 1 : 0) + (modem_file ? 1 : 0);
	fds = calloc(sizeof(struct pollfd), nfds);
	
	poll_i = 0;
	if (need_sound) {
		sound_poll_fill_tx(fds, sound_fdc_tx);
		sound_poll_fill_rx(fds + sound_fdc_tx, sound_fdc_rx);
		poll_i += sound_fdc_tx + sound_fdc_rx;
	}
	poll_int = poll_i++;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;
	if (nmea) {
		poll_nmea = poll_i++;
		fds[poll_nmea].fd = fd_nmea;
		fds[poll_nmea].events = POLLIN;
	}
	if (modem_file) {
		poll_modem = poll_i++;
		fds[poll_modem].fd = fd_modem;
	}
	
	do {
		if (modem_file) {
			freedv_eth_modem_poll(&fds[poll_modem].events);
		}
		
		poll(fds, nfds, -1);

		bool do_tx_state_machine;
		
		if (modem_file) {
			do_tx_state_machine = freedv_eth_modem_tx_empty(fd_modem);
		} else {
			do_tx_state_machine = need_sound && sound_poll_out_tx(fds, sound_fdc_tx);
		}
		if (do_tx_state_machine) {
			if (tx_mode == TX_MODE_MIXED) {
				bool q_v = queue_voice_filled(1);
				bool q_d = queue_data_filled() || queue_control_filled();
				bool tx_v = freedv_eth_txa_ptt();
				bool tx_d = freedv_eth_tx_ptt();

				/* voice has preference */
				if (!tx_v && !q_v && (q_d || tx_d)) {
					freedv_eth_tx_state_machine();
				} else {
					freedv_eth_txa_state_machine();
				}
			} else if (tx_mode == TX_MODE_FREEDV)
				freedv_eth_tx_state_machine();
			else if (tx_mode == TX_MODE_ANALOG)
				freedv_eth_txa_state_machine();
			else
				freedv_eth_tx_none(nr_samples);
		}
		if (fds[poll_int].revents & POLLIN) {
			interface_tx_raw(cb_int_tx);
		}
		if (need_sound && sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
		if (nmea && fds[poll_nmea].revents & POLLIN) {
			read_nmea(fd_nmea);
		}
		if (modem_file) {
			if (fds[poll_modem].revents & POLLIN) {
				freedv_eth_modem_rx(fd_modem);
			}
			if (fds[poll_modem].revents & POLLOUT) {
				freedv_eth_modem_tx(fd_modem);
			}
		}
	} while (1);
	
	
	return 0;
}
