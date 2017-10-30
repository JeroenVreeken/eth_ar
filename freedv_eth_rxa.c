/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2017

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

#include "freedv_eth.h"
#include "freedv_eth_config.h"
#include "io.h"
#include "dtmf.h"
#include "emphasis.h"
#include "interface.h"
#include "alaw.h"
#include "sound.h"
#include "ctcss.h"
#include "eth_ar_codec2.h"

#include <string.h>

static struct emphasis *emphasis_d = NULL;
static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static bool dtmf_initialized = false;
static bool cdc;
static bool ctcss_sql;
static int dtmf_mute = 1;
static struct sound_resample *sr = NULL;
static float rx_gain = 1.0;
static int voice_mode = CODEC2_MODE_ALAW;
static char dtmf_control_start = '*';
static char dtmf_control_stop = '#';

enum dtmf_state {
	DTMF_IDLE,
	DTMF_CONTROL,
	DTMF_CONTROL_TAIL,
};

static enum dtmf_state dtmf_state = DTMF_IDLE;

bool freedv_eth_rxa_cdc(void)
{
	return cdc;
}



static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	if (ctrl[0] == dtmf_control_start) {
		dtmf_state = DTMF_CONTROL;
	} else if (ctrl[0] == dtmf_control_stop) {
		dtmf_state = DTMF_CONTROL_TAIL;
	} else {
		if (dtmf_state == DTMF_CONTROL_TAIL)
			dtmf_state = DTMF_IDLE;
	}
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl));
}


void freedv_eth_rxa(int16_t *samples, int nr)
{
	int nr_a = sound_resample_nr_out(sr, nr);
	int16_t mod_a[nr_a];
	bool detected;

	sound_resample_perform_gain_limit(sr, mod_a, samples, nr_a, nr, rx_gain);

	cdc = io_hl_dcd_get();

	if (emphasis_d)
		emphasis_de(emphasis_d, mod_a, nr_a);
	if (ctcss_sql) {
		cdc |= ctcss_detect_rx(mod_a, nr_a);
	}

	dtmf_rx(mod_a, nr_a, cb_control, &detected);
	if (detected) {
		if ((dtmf_mute == 1) ||
		    (dtmf_mute == 2 && dtmf_state == DTMF_CONTROL) ||
		    (dtmf_mute == 2 && dtmf_state == DTMF_CONTROL_TAIL))
			memset(mod_a, 0, nr_a * sizeof(int16_t)); 
	}

	if (cdc)
		switch (voice_mode) {
			case CODEC2_MODE_ALAW: {
				uint8_t alaw[nr_a];
		
				alaw_encode(alaw, mod_a, nr_a);
				interface_rx(bcast, mac, ETH_P_ALAW, alaw, nr_a);
			}
			case CODEC2_MODE_NATIVE16: {
				interface_rx(bcast, mac, ETH_P_NATIVE16, (uint8_t *)mod_a, nr_a * sizeof(int16_t));
			}
		}
	else {
		dtmf_state = DTMF_IDLE;
	}
}

int freedv_eth_rxa_init(int hw_rate, uint8_t mac_init[6], 
    bool emphasis, double ctcss_freq, int dtmf_mute_init,
    float rx_gain_init)
{
	int a_rate = FREEDV_ALAW_RATE;
	bool use_short = atoi(freedv_eth_config_value("analog_rx_short", NULL, "0"));
	
	if (use_short) {
		voice_mode = CODEC2_MODE_NATIVE16;
	}
	
	rx_gain = rx_gain_init;
	memcpy(mac, mac_init, 6);
	printf("Analog rx gain: %f\n", rx_gain);

	cdc = false;

	sound_resample_destroy(sr);
	sr = sound_resample_create(a_rate, hw_rate);

	if (!dtmf_initialized) {
		dtmf_init();
		dtmf_initialized = true;
	}
	dtmf_mute = dtmf_mute_init;
	
	emphasis_destroy(emphasis_d);
	emphasis_d = NULL;
	if (emphasis)
		emphasis_d = emphasis_init();

	if (ctcss_freq > 0.0) {
		ctcss_detect_init(ctcss_freq);
		ctcss_sql = true;
	} else {
		ctcss_sql = false;
	}
	return 0;
}
