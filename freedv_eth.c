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
#include <eth_ar/eth_ar.h>
#include "sound.h"
#include <eth_ar/fprs.h>
#include "nmea.h"
#include "freedv_eth_rx.h"
#include "freedv_eth.h"
#include "freedv_eth_config.h"
#include "io.h"

static bool verbose;
static bool fullduplex;
static bool vc_control;

static struct freedv *freedv;
static int freedv_rx_channel;
static int tx_codecmode;

static int analog_rx_channel;

static int tx_delay_msec;
static int tx_tail_msec;
static int tx_header_msec = 500;
static int tx_header_max_msec = 5000;
static int tx_fprs_msec = 30000;
static bool freedv_hasdata;
static uint8_t mac[6];

static struct nmea_state *nmea;

enum tx_mode {
	TX_MODE_FREEDV,
	TX_MODE_ANALOG,
};

static enum tx_mode tx_mode;

enum rx_mode {
	RX_MODE_NONE,
	RX_MODE_FREEDV,
	RX_MODE_ANALOG,
	RX_MODE_MIXED,
};

static enum rx_mode rx_mode;

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




static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct tx_packet *packet;
	
	if (freedv_eth_type_isvoice(eth_type)) {
		packet = tx_packet_alloc();
		packet->len = len;
		memcpy(packet->data, data, len);
		memcpy(packet->from, from, 6);
		
		freedv_eth_transcode(packet, tx_codecmode, eth_type);

		enqueue_voice(packet);
	} else if (tx_mode == TX_MODE_FREEDV) {
//		printf("Data: %d %x\n", eth_type, eth_type);
		/* TODO: send control as DTMF in analog mode */
		if (eth_type == ETH_P_AR_CONTROL && vc_control) {
			packet = tx_packet_alloc();
			memcpy(packet->data, data, len);
			packet->len = len;
			packet->off = 0;
			
			enqueue_control(packet);
		} else if (freedv_hasdata) {
			packet = tx_packet_alloc();
			packet->len = len + 6 + 6 + 2;
			memcpy(packet->data + 0, to, 6);
			memcpy(packet->data + 6, from, 6);
			packet->data[12] = eth_type >> 8;
			packet->data[13] = eth_type & 0xff;
			memcpy(packet->data + 14, data, len);

			enqueue_data(packet);
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
	struct pollfd *fds;
	int sound_fdc_tx;
	int sound_fdc_rx;
	int nfds;
	int poll_int;
	int poll_nmea = 0;
	uint16_t type;
	int nr_samples;
	int freedv_mode;
	rig_model_t rig_model;
	ptt_type_t ptt_type = RIG_PTT_NONE;
	dcd_type_t dcd_type = RIG_DCD_NONE;

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
	verbose = atoi(freedv_eth_config_value("verbose", NULL, "0"));
	char *freedv_mode_str = freedv_eth_config_value("freedv_mode", NULL, "700");
	rig_model = atoi(freedv_eth_config_value("rig_model", NULL, "1"));
	char *ptt_file = freedv_eth_config_value("rig_ptt_file", NULL, NULL);
	vc_control = atoi(freedv_eth_config_value("control_vc", NULL, "0"));
	char *rig_ptt_type = freedv_eth_config_value("rig_ptt_type", NULL, "NONE");
	char *rig_dcd_type = freedv_eth_config_value("rig_dcd_type", NULL, "NONE");
	char *freedv_rx_sound_channel = freedv_eth_config_value("freedv_rx_sound_channel", NULL, "left");
	char *analog_rx_sound_channel = freedv_eth_config_value("analog_rx_sound_channel", NULL, "left");
	char *tx_mode_str = freedv_eth_config_value("tx_mode", NULL, "freedv");
	char *rx_mode_str = freedv_eth_config_value("rx_mode", NULL, "freedv");
	double rx_ctcss_f = atof(freedv_eth_config_value("analog_rx_ctcss_frequency", NULL, "0.0"));
	double tx_ctcss_f = atof(freedv_eth_config_value("analog_tx_ctcss_frequency", NULL, "0.0"));
	double tx_ctcss_amp = atof(freedv_eth_config_value("analog_tx_ctcss_amp", NULL, "0.15"));
	int beacon_interval = atoi(freedv_eth_config_value("analog_tx_beacon_interval", NULL, "0"));
	char *beacon_msg = freedv_eth_config_value("analog_tx_beacon_message", NULL, "");
	bool tx_emphasis = atoi(freedv_eth_config_value("analog_tx_emphasis", NULL, "0"));
	bool rx_emphasis = atoi(freedv_eth_config_value("analog_rx_emphasis", NULL, "0"));
	int dcd_threshold = atoi(freedv_eth_config_value("analog_rx_dcd_threshold", NULL, "1"));
	
	if (!strcmp(freedv_mode_str, "1600")) {
		freedv_mode = FREEDV_MODE_1600;
		freedv_hasdata = false;
	} else if (!strcmp(freedv_mode_str, "700")) {
		freedv_mode = FREEDV_MODE_700;
		freedv_hasdata = false;
	} else if (!strcmp(freedv_mode_str, "700B")) {
		freedv_mode = FREEDV_MODE_700B;
		freedv_hasdata = false;
	} else if (!strcmp(freedv_mode_str, "700C")) {
		freedv_mode = FREEDV_MODE_700C;
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
	} else {
		printf("Invalid FreeDV mode\n");
		return -1;
	}
	
	if (!strcmp(tx_mode_str, "freedv")) {
		tx_mode = TX_MODE_FREEDV;
	} else if (!strcmp(tx_mode_str, "analog")) {
		tx_mode = TX_MODE_ANALOG;
	} else {
		printf("Invalid tx_mode\n");
		return -1;
	}
	
	if (!strcmp(rx_mode_str, "none")) {
		rx_mode = RX_MODE_NONE;
	} else if (!strcmp(rx_mode_str, "freedv")) {
		rx_mode = RX_MODE_FREEDV;
	} else if (!strcmp(rx_mode_str, "analog")) {
		rx_mode = RX_MODE_ANALOG;
	} else if (!strcmp(rx_mode_str, "mixed")) {
		rx_mode = RX_MODE_MIXED;
	} else {
		printf("Invalid rx_mode\n");
		return -1;
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
	else
		dcd_type = atoi(rig_dcd_type);

	io_hl_init(rig_model, dcd_threshold, ptt_type, ptt_file, dcd_type);
	
	type = freedv_eth_mode2type(freedv_mode);
	
	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		return -1;
	}
	
	freedv = freedv_open(freedv_mode);

	freedv_set_callback_txt(freedv, freedv_eth_rx_vc_callback, freedv_eth_tx_vc_callback, NULL);
	freedv_set_callback_data(freedv, freedv_eth_rx_cb_datarx, freedv_eth_tx_cb_datatx, NULL);
	freedv_set_data_header(freedv, mac);

	if (tx_mode == TX_MODE_FREEDV) {
		tx_codecmode = freedv_eth_type2codecmode(type);
	} else {
		/* Decode to speech shorts, but don't recode... */
		tx_codecmode = 'S';
	}
	fd_int = interface_init(netname, mac, true, 0);
	sound_rate = sound_init(sounddev, cb_sound_in, sound_rate, 2);

	if (tx_mode == TX_MODE_FREEDV) {
		int freedv_rate = freedv_get_modem_sample_rate(freedv);
		printf("freedv sample rate: %d\n", freedv_rate);
		nr_samples = freedv_get_n_nom_modem_samples(freedv) * sound_rate / freedv_rate;
	} else {
		nr_samples = FREEDV_ALAW_NR_SAMPLES * sound_rate / FREEDV_ALAW_RATE;
	}
	printf("nom number of modem samples: %d\n", nr_samples);

	sound_set_nr(nr_samples);

	freedv_eth_rx_init(freedv, mac, sound_rate);
	freedv_eth_rxa_init(sound_rate, mac, rx_emphasis, rx_ctcss_f);


	if (tx_mode == TX_MODE_FREEDV) {
		freedv_eth_tx_init(freedv, mac, nmea, fullduplex,
		    sound_rate,
		    tx_tail_msec, tx_delay_msec,
		    tx_header_msec, tx_header_max_msec,
		    tx_fprs_msec);
	} else {
		freedv_eth_txa_init(fullduplex, 
		    sound_rate, 
		    tx_tail_msec,
		    tx_ctcss_f, tx_ctcss_amp,
		    beacon_interval, beacon_msg,
		    tx_emphasis);
	}
	
	if (nmeadev) {
		fd_nmea = open(nmeadev, O_RDONLY);
		if (fd_nmea >= 0) {
			nmea = nmea_state_create();
			printf("GPS device: %s\n", nmeadev);
		}
	}


	prio();
	
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	sound_fdc_tx = sound_poll_count_tx();
	sound_fdc_rx = sound_poll_count_rx();
	nfds = sound_fdc_tx + sound_fdc_rx + 1 + (nmea ? 1 : 0);
	fds = calloc(sizeof(struct pollfd), nfds);
	
	sound_poll_fill_tx(fds, sound_fdc_tx);
	sound_poll_fill_rx(fds + sound_fdc_tx, sound_fdc_rx);
	poll_int = sound_fdc_tx + sound_fdc_rx;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;
	if (nmea) {
		poll_nmea = poll_int + 1;
		fds[poll_nmea].fd = fd_nmea;
		fds[poll_nmea].events = POLLIN;
	}
	
	do {
		poll(fds, nfds, -1);
		if (sound_poll_out_tx(fds, sound_fdc_tx)) {
			if (tx_mode == TX_MODE_FREEDV)
				freedv_eth_tx_state_machine();
			else
				freedv_eth_txa_state_machine();
		}
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
		}
		if (sound_poll_in_rx(fds + sound_fdc_tx, sound_fdc_rx)) {
			sound_rx();
		}
		if (nmea && fds[poll_nmea].revents & POLLIN) {
			read_nmea(fd_nmea);
		}
	} while (1);
	
	
	return 0;
}
